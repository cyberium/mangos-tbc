#
# This file is part of the CMaNGOS Project. See AUTHORS file for Copyright information
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
#

include(FetchContent)
# avoid online check when already downloaded (little speed up)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)

if(BUILD_EXTRACTORS AND NOT (BZip2_FOUND OR BZIP2_FOUND))
  add_subdirectory(bzip2)
endif()

# === zlib dependency resolution ===
# Always use FetchContent for zlib 1.3.1
message(STATUS "zlib: fetching version 1.3.1 from source via CMake")

include(FetchContent)

FetchContent_Declare(
  zlib
  GIT_REPOSITORY "https://github.com/madler/zlib.git"
  GIT_TAG 5a82f71ed1dfc0bec044d9702463dbdf84ea3b71 # v1.3.1 in develop branch from 19/02/2025
  SYSTEM  # suppresses compiler warnings from zlib headers
)

set(SKIP_INSTALL_ALL ON CACHE INTERNAL "")
set(ZLIB_ENABLE_TESTS OFF CACHE INTERNAL "")
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")

FetchContent_MakeAvailable(zlib)

get_target_property(_zlib_ver zlib VERSION)
message(STATUS "zlib: using fetched version ${_zlib_ver}")

# === Boost dependency resolution ===
# Always use FetchContent for Boost 1.85.0
if (BUILD_GAME_SERVER OR BUILD_LOGIN_SERVER OR BUILD_EXTRACTORS)
  message(STATUS "Boost: fetching version 1.85.0 from source via CMake")

  FetchContent_Declare(
    Boost
    URL https://github.com/boostorg/boost/releases/download/boost-1.85.0/boost-1.85.0-cmake.tar.gz
    URL_HASH SHA256=ab9c9c4797384b0949dd676cf86b4f99553f8c148d767485aaac412af25183e6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE  # Use extraction time for timestamps (CMake 3.24+ policy CMP0135)
    SYSTEM  # suppresses compiler warnings from Boost headers
  )

  # Only include required Boost libraries
  set(BOOST_INCLUDE_LIBRARIES
    headers
    program_options
    thread
    filesystem
    asio
    circular_buffer
    date_time
  )
  set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "")

  # Make Boost targets GLOBAL for visibility across subdirectories (CMake 3.24+)
  set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)

  FetchContent_MakeAvailable(Boost)

  message(STATUS "Boost: using fetched version 1.85.0")
endif()

# === Validate Boost targets ===
foreach(_mod IN LISTS BOOST_INCLUDE_LIBRARIES)
  # Compose expected target name (e.g. Boost::asio)
  set(_target "Boost::${_mod}")

  if (NOT TARGET "${_target}")
    message(FATAL_ERROR
      "Boost module '${_mod}' was requested in BOOST_INCLUDE_LIBRARIES, "
      "but Boost target '${_target}' does not exist in Boost 1.85.0.\n"
      "Check for typos or missing modules."
    )
  else()
    message(STATUS "Boost: found target ${_target}")
  endif()
endforeach()


if (BUILD_GAME_SERVER OR BUILD_LOGIN_SERVER)
  add_subdirectory(gsoap)
endif()

if (BUILD_GAME_SERVER OR BUILD_LOGIN_SERVER OR BUILD_EXTRACTORS)
  add_subdirectory(utf8cpp)
endif()
