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

#ifndef CMANGOS_LOOT_GROUP_ROLL_H
#define CMANGOS_LOOT_GROUP_ROLL_H

#include "Common.h"
#include "Timer.h"
#include "LootDefines.h"
#include "Entities/ObjectGuid.h"

#include <memory>

class Player;
class LootBase;
struct LootItem;
typedef std::shared_ptr<LootItem> LootItemSPtr;

struct PlayerRollVote
{
    PlayerRollVote() : vote(ROLL_NOT_VALID), number(0) {}
    RollVote vote;
    uint8    number;
};

class GroupLootRoll
{
public:
    typedef std::unordered_map<ObjectGuid, PlayerRollVote> RollVoteMap;

    GroupLootRoll() : m_rollVoteMap(ROLL_VOTE_MASK_ALL), m_isStarted(false), m_lootItem(nullptr), m_loot(nullptr), m_voteMask()
    {}
    ~GroupLootRoll();

    bool TryToStart(LootBase& loot, LootItemSPtr& lootItem);
    bool PlayerVote(Player* player, RollVote vote);
    bool UpdateRoll(uint32 diff);

private:
    void SendStartRoll();
    void SendAllPassed();
    void SendRoll(ObjectGuid const& targetGuid, uint32 rollNumber, uint32 rollType);
    void SendLootRollWon(ObjectGuid const& targetGuid, uint32 rollNumber, RollVote rollType);
    void Finish(RollVoteMap::const_iterator& winnerItr);
    bool AllPlayerVoted(RollVoteMap::const_iterator& winnerItr);
    RollVoteMap           m_rollVoteMap;
    bool                  m_isStarted;
    LootItemSPtr          m_lootItem;
    LootBase*             m_loot;
    RollVoteMask          m_voteMask;
    ShortTimeTracker      m_rollTimer;
};
typedef std::unordered_map<uint32, GroupLootRoll> GroupLootRollMap;

#endif
