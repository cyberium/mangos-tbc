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

#include "LootGroupRoll.h"
#include "LootItem.h"
#include "Loot.h"
#include "WorldPacket.h"
#include "Globals/ObjectMgr.h"

// Send the roll for the whole group
void GroupLootRoll::SendStartRoll()
{
    WorldPacket data(SMSG_LOOT_START_ROLL, (8 + 4 + 4 + 4 + 4 + 4 + 1));
    data << m_loot->GetLootGuid();                          // creature guid what we're looting
    data << uint32(m_itemSlot);                             // item slot in loot
    data << uint32(m_lootItem->itemId);                     // the itemEntryId for the item that shall be rolled for
    data << uint32(m_lootItem->randomSuffix);               // randomSuffix
    data << uint32(m_lootItem->randomPropertyId);           // item random property ID
    data << uint32(LOOT_ROLL_TIMEOUT);                      // the countdown time to choose "need" or "greed"

    size_t voteMaskPos = data.wpos();
    data << uint8(0);

    for (RollVoteMap::const_iterator itr = m_rollVoteMap.begin(); itr != m_rollVoteMap.end(); ++itr)
    {
        if (itr->second.vote == ROLL_NOT_VALID)
            continue;

        Player* plr = sObjectMgr.GetPlayer(itr->first);
        if (!plr || !plr->GetSession())
            continue;
        // dependent from player
        RollVoteMask mask = m_voteMask;
        // In NEED_BEFORE_GREED need disabled for non-usable item for player
        if (m_loot->m_lootMethod == NEED_BEFORE_GREED && plr->CanUseItem(m_lootItem->itemProto) != EQUIP_ERR_OK)
            mask = RollVoteMask(mask & ~ROLL_VOTE_MASK_NEED);
        data.put<uint8>(voteMaskPos, uint8(mask));

        plr->GetSession()->SendPacket(data);
    }
}

// Send all passed message
void GroupLootRoll::SendAllPassed()
{
    WorldPacket data(SMSG_LOOT_ALL_PASSED, (8 + 4 + 4 + 4 + 4));
    data << m_loot->GetLootGuid();                          // creature guid what we're looting
    data << uint32(m_itemSlot);                             // item slot in loot
    data << uint32(m_lootItem->itemId);                     // the itemEntryId for the item that shall be rolled for
    data << uint32(m_lootItem->randomSuffix);               // randomSuffix
    data << uint32(m_lootItem->randomPropertyId);           // item random property ID

    for (RollVoteMap::const_iterator itr = m_rollVoteMap.begin(); itr != m_rollVoteMap.end(); ++itr)
    {
        if (itr->second.vote == ROLL_NOT_VALID)
            continue;

        Player* plr = sObjectMgr.GetPlayer(itr->first);
        if (!plr || !plr->GetSession())
            continue;

        plr->GetSession()->SendPacket(data);
    }
}

// Send roll 'value' of the whole group and the winner to the whole group
void GroupLootRoll::SendLootRollWon(ObjectGuid const& targetGuid, uint32 rollNumber, RollVote rollType)
{
    WorldPacket data(SMSG_LOOT_ROLL_WON, (8 + 4 + 4 + 4 + 4 + 8 + 1 + 1));
    data << m_loot->GetLootGuid();                          // creature guid what we're looting
    data << uint32(m_itemSlot);                             // item slot in loot
    data << uint32(m_lootItem->itemId);                     // the itemEntryId for the item that shall be rolled for
    data << uint32(m_lootItem->randomSuffix);               // randomSuffix
    data << uint32(m_lootItem->randomPropertyId);           // item random property ID
    data << targetGuid;                                     // guid of the player who won.
    data << uint8(rollNumber);                              // rollnumber related to SMSG_LOOT_ROLL
    data << uint8(rollType);                                // Rolltype related to SMSG_LOOT_ROLL

    for (RollVoteMap::const_iterator itr = m_rollVoteMap.begin(); itr != m_rollVoteMap.end(); ++itr)
    {
        switch (itr->second.vote)
        {
            case ROLL_PASS:
                break;
            case ROLL_NOT_EMITED_YET:
            case ROLL_NOT_VALID:
                SendRoll(itr->first, 128, 128);
                break;
            default:
                SendRoll(itr->first, itr->second.number, itr->second.vote);
                break;
        }
    }

    for (RollVoteMap::const_iterator itr = m_rollVoteMap.begin(); itr != m_rollVoteMap.end(); ++itr)
    {
        if (itr->second.vote == ROLL_NOT_VALID)
            continue;

        Player* plr = sObjectMgr.GetPlayer(itr->first);
        if (!plr || !plr->GetSession())
            continue;
        plr->GetSession()->SendPacket(data);
    }
}

// Send roll of targetGuid to the whole group (included targuetGuid)
void GroupLootRoll::SendRoll(ObjectGuid const& targetGuid, uint32 rollNumber, uint32 rollType)
{
    WorldPacket data(SMSG_LOOT_ROLL, (8 + 4 + 8 + 4 + 4 + 4 + 1 + 1 + 1));
    data << m_loot->GetLootGuid();                          // creature guid what we're looting
    data << uint32(m_itemSlot);                             // item slot in loot
    data << targetGuid;
    data << uint32(m_lootItem->itemId);                     // the itemEntryId for the item that shall be rolled for
    data << uint32(m_lootItem->randomSuffix);               // randomSuffix
    data << uint32(m_lootItem->randomPropertyId);           // item random property ID
    data << uint8(rollNumber);                              // 0: "Need for: [item name]" > 127: "you passed on: [item name]"      Roll number
    data << uint8(rollType);                                // 0: "Need for: [item name]" 0: "You have selected need for [item name] 1: need roll 2: greed roll
    data << uint8(0);                                       // auto pass on loot

    for (RollVoteMap::const_iterator itr = m_rollVoteMap.begin(); itr != m_rollVoteMap.end(); ++itr)
    {
        if (itr->second.vote == ROLL_NOT_VALID)
            continue;

        Player* plr = sObjectMgr.GetPlayer(itr->first);
        if (!plr || !plr->GetSession())
            continue;

        plr->GetSession()->SendPacket(data);
    }
}

GroupLootRoll::~GroupLootRoll()
{
    if (m_isStarted)
        SendAllPassed();
}

// Try to start the group roll for the specified item (it may fail for quest item or any condition
// If this method return false the roll have to be removed from the container to avoid any problem
bool GroupLootRoll::TryToStart(Loot& loot, uint32 itemSlot)
{
    if (!m_isStarted)
    {
        if (itemSlot >= loot.m_lootItems.size())
            return false;

        // initialize the data needed for the roll
        m_lootItem = loot.GetLootItemInSlot(itemSlot);

        if (m_lootItem->lootItemType == LOOTITEM_TYPE_QUEST)
            return false;

        m_loot = &loot;
        m_itemSlot = itemSlot;
        m_lootItem->isBlocked = true;                           // block the item while rolling

        uint32 playerCount = 0;
        for (auto itr : m_loot->m_ownerSet)
        {
            Player* plr = sObjectMgr.GetPlayer(itr);
            if (!plr || !m_lootItem->IsAllowed(plr, m_loot))    // check if player meet the condition to be able to roll this item
            {
                m_rollVoteMap[itr].vote = ROLL_NOT_VALID;
                continue;
            }
            m_rollVoteMap[itr].vote = ROLL_NOT_EMITED_YET;      // initialize player vote map
            ++playerCount;
        }

        // initialize item prototype
        m_voteMask = ROLL_VOTE_MASK_ALL;

        if (playerCount > 1)                                    // check if more than one player can loot this item
        {
            // start the roll
            SendStartRoll();
            m_endTime = time(nullptr) + (LOOT_ROLL_TIMEOUT / 1000);
            m_isStarted = true;
            return true;
        }
        // no need to start roll if one or less player can loot this item so place it under threshold
        m_lootItem->isUnderThreshold = true;
        m_lootItem->isBlocked = false;
    }
    return false;
}

// Add vote from playerGuid
bool GroupLootRoll::PlayerVote(Player* player, RollVote vote)
{
    ObjectGuid const& playerGuid = player->GetObjectGuid();
    RollVoteMap::iterator voterItr = m_rollVoteMap.find(playerGuid);
    if (voterItr == m_rollVoteMap.end())
        return false;

    voterItr->second.vote = vote;

    if (vote != ROLL_PASS && vote != ROLL_NOT_VALID)
        voterItr->second.number = urand(1, 100);

    switch (vote)
    {
        case ROLL_PASS:                                     // Player choose pass
        {
            SendRoll(playerGuid, 128, 128);
            break;
        }
        case ROLL_NEED:                                     // player choose Need
        {
            SendRoll(playerGuid, 0, 0);
            break;
        }
        case ROLL_GREED:                                    // player choose Greed
        {
            SendRoll(playerGuid, 128, 2);
            break;
        }
        default:                                            // Roll removed case
            return false;
    }
    return true;
}

// check if we can found a winner for this roll or if timer is expired
bool GroupLootRoll::UpdateRoll()
{
    RollVoteMap::const_iterator winnerItr = m_rollVoteMap.end();

    if (AllPlayerVoted(winnerItr) || m_endTime <= time(nullptr))
    {
        Finish(winnerItr);
        return true;
    }
    return false;
}

/**
* \brief: Check if all player have voted and return true in that case. Also return current winner.
* \param: RollVoteMap::const_iterator& winnerItr > will be different than m_rollCoteMap.end() if winner exist. (Someone voted greed or need)
* \returns: bool > true if all players voted
**/
bool GroupLootRoll::AllPlayerVoted(RollVoteMap::const_iterator& winnerItr)
{
    uint32 notVoted = 0;
    bool isSomeoneNeed = false;

    winnerItr = m_rollVoteMap.end();
    for (RollVoteMap::const_iterator itr = m_rollVoteMap.begin(); itr != m_rollVoteMap.end(); ++itr)
    {
        switch (itr->second.vote)
        {
            case ROLL_NEED:
                if (!isSomeoneNeed || winnerItr == m_rollVoteMap.end() || itr->second.number > winnerItr->second.number)
                {
                    isSomeoneNeed = true;                                               // first passage will force to set winner because need is prioritized
                    winnerItr = itr;
                }
                break;
            case ROLL_GREED:
            case ROLL_DISENCHANT:
                if (!isSomeoneNeed)                                                      // if at least one need is detected then winner can't be a greed
                {
                    if (winnerItr == m_rollVoteMap.end() || itr->second.number > winnerItr->second.number)
                        winnerItr = itr;
                }
                break;
                // Explicitly passing excludes a player from winning loot, so no action required.
            case ROLL_PASS:
                break;
            case ROLL_NOT_EMITED_YET:
                ++notVoted;
                break;
            default:
                break;
        }
    }

    return notVoted == 0;
}

// terminate the roll
void GroupLootRoll::Finish(RollVoteMap::const_iterator& winnerItr)
{
    m_lootItem->isBlocked = false;
    if (winnerItr == m_rollVoteMap.end())
    {
        SendAllPassed();
        m_lootItem->isReleased = true;
    }
    else
    {
        SendLootRollWon(winnerItr->first, winnerItr->second.number, winnerItr->second.vote);

        Player* plr = sObjectMgr.GetPlayer(winnerItr->first);
        if (plr && plr->GetSession())
        {
            m_loot->SendItem(plr, m_itemSlot);
        }
        else
        {
            // hum the winner is not available
            m_lootItem->isReleased = true;
        }
    }
    m_isStarted = false;
}

GroupLootRoll* Loot::GetRollForSlot(uint32 itemSlot)
{
    GroupLootRollMap::iterator rollItr = m_roll.find(itemSlot);
    if (rollItr == m_roll.end())
        return nullptr;
    return &rollItr->second;
}