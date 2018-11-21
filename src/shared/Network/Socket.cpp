/*
* This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "Socket.hpp"
#include "Log.h"

#include <boost/asio.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/lexical_cast.hpp>

#include <string>
#include <memory>
#include <utility>
#include <vector>
#include <functional>
#include <cstring>

namespace MaNGOS
{
    Socket::Socket(boost::asio::io_service& service, std::function<void (Socket*)> closeHandler)
        : m_readState(ReadState::Idle), m_socket(service),
          m_closeHandler(std::move(closeHandler)), m_address("0.0.0.0"),
          m_activeBuffer(0), m_writeTimer(service) {}

    bool Socket::Open()
    {
        // set tcp no delay preference (enable/disable Nagle algorithm)
        m_socket.set_option(boost::asio::ip::tcp::no_delay(false));

        try
        {
            const_cast<std::string&>(m_address) = m_socket.remote_endpoint().address().to_string();
            const_cast<std::string&>(m_remoteEndpoint) = boost::lexical_cast<std::string>(m_socket.remote_endpoint());
        }
        catch (boost::system::error_code& error)
        {
            sLog.outError("Socket::Open() failed to get remote address.  Error: %s", error.message().c_str());
            return false;
        }
        m_inBuffer.reset(new PacketBuffer);

        StartAsyncRead();

        return true;
    }

    void Socket::Close()
    {
        std::lock_guard<std::mutex> guard(m_closeMutex);
        if (IsClosed())
            return;

        boost::system::error_code ec;
        m_socket.shutdown(boost::asio::ip::tcp::socket::shutdown_both, ec);
        m_socket.close();

        if (m_closeHandler)
            m_closeHandler(this);
    }

    void Socket::StartAsyncRead()
    {
        if (IsClosed())
        {
            m_readState = ReadState::Idle;
            return;
        }

        std::shared_ptr<Socket> ptr = shared<Socket>();
        m_readState = ReadState::Reading;
        m_socket.async_read_some(boost::asio::buffer(&m_inBuffer->m_buffer[m_inBuffer->m_writePosition], m_inBuffer->m_buffer.size() - m_inBuffer->m_writePosition),
                                 make_custom_alloc_handler(m_allocator,
        [ptr](const boost::system::error_code & error, size_t length) { ptr->OnRead(error, length); }));
    }

    void Socket::OnRead(const boost::system::error_code& error, size_t length)
    {
        if (error)
        {
            m_readState = ReadState::Idle;
            OnError(error);
            return;
        }

        if (IsClosed())
        {
            m_readState = ReadState::Idle;
            return;
        }

        m_inBuffer->m_writePosition += length;

        const size_t available = m_socket.available();

        // if there is still data to read, increase the buffer size and do so (if necessary)
        if (available > 0 && (length + available) > m_inBuffer->m_buffer.size())
        {
            m_inBuffer->m_buffer.resize(m_inBuffer->m_buffer.size() + available);
            StartAsyncRead();
            return;
        }

        // we must repeat this in case we have read in multiple messages from the client
        while (m_inBuffer->m_readPosition < m_inBuffer->m_writePosition)
        {
            if (!ProcessIncomingData())
            {
                // this errno is set when there is not enough buffer data available to either complete a header, or the packet length
                // specified in the header goes past what we've read.  in this case, we will reset the buffer with the remaining data
                if (errno == EBADMSG)
                {
                    const size_t bytesRemaining = m_inBuffer->m_writePosition - m_inBuffer->m_readPosition;

                    ::memmove(&m_inBuffer->m_buffer[0], &m_inBuffer->m_buffer[m_inBuffer->m_readPosition], bytesRemaining);

                    m_inBuffer->m_readPosition = 0;
                    m_inBuffer->m_writePosition = bytesRemaining;

                    StartAsyncRead();
                }
                else if (!IsClosed())
                    Close();

                return;
            }
        }

        // at this point, the packet has been read and successfully processed.  reset the buffer.
        m_inBuffer->m_writePosition = m_inBuffer->m_readPosition = 0;

        StartAsyncRead();
    }

    void Socket::OnError(const boost::system::error_code& error)
    {
        // skip logging this code because it happens whenever anyone disconnects.  reduces spam.
        if (error != boost::asio::error::eof &&
                error != boost::asio::error::operation_aborted)
            sLog.outBasic("Socket::OnError.  %s.  Connection closed.", error.message().c_str());

        if (!IsClosed())
            Close();
    }

    bool Socket::Read(char* buffer, int length)
    {
        if (ReadLengthRemaining() < length)
            return false;

        m_inBuffer->Read(buffer, length);
        return true;
    }

    void Socket::Write(BytesContainerSPtr& data)
    {
        std::lock_guard<std::mutex> guard(m_mutex);

        // move input data to the inactive buffer (^1 doing xor so the result will be 1 or 0)
        m_messagesList[m_activeBuffer ^ 1].push_back(data);

        if (m_sendingBuffer.empty())
            DoAsyncWrite();
    }

    void Socket::Write(const char* message, uint32 size)
    {
        MaNGOS::BytesContainerSPtr messageData = MaNGOS::BytesContainerSPtr(new MaNGOS::BytesContainer(message, message + size));
        Write(messageData);
    }

    void Socket::DoAsyncWrite()
    {
        m_activeBuffer ^= 1; // switch buffers

        // fill sending buffer
        for (const auto& data : m_messagesList[m_activeBuffer])
            m_sendingBuffer.push_back(boost::asio::buffer(*data));

        // setting up an timer to protect against any timeout
        m_writeTimer.expires_from_now(boost::posix_time::milliseconds(int(WriteTimeout)));
        std::shared_ptr<Socket> ptr = shared<Socket>();
        m_writeTimer.async_wait([ptr](const boost::system::error_code & error)
        {
            if (!error) {
                // timeout error
                ptr->OnError(error);
            }
        });

        // send asynchronously data in m_messageList[m_activeBuffer]
        boost::asio::async_write(m_socket, m_sendingBuffer,
            [this, self = shared_from_this()](const boost::system::error_code& error, size_t bytes_transferred)
        {
            // cancel timeout check
            m_writeTimer.cancel();
            std::lock_guard<std::mutex> guard(m_mutex);

            // we can clear the message list everything is sent
            m_messagesList[m_activeBuffer].clear();
            m_sendingBuffer.clear();

            if (!error)
            {
                // check if there is some new data to send
                if (!m_messagesList[m_activeBuffer ^ 1].empty())
                    DoAsyncWrite();
            }
            else
            {
                // error while sending the data
                OnError(error);
            }
        });
    }
}
