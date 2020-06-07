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

#ifndef CMANGOS_LOOT_DEFINES_H
#define CMANGOS_LOOT_DEFINES_H

#include <memory>

#define MAX_NR_LOOT_ITEMS 16
// note: the client cannot show more than 16 items total

// For Loot system
enum CreatureLootStatus
{
    CREATURE_LOOT_STATUS_NONE           = 0,
    CREATURE_LOOT_STATUS_PICKPOCKETED   = 1,
    CREATURE_LOOT_STATUS_LOOTED         = 2,
    CREATURE_LOOT_STATUS_SKIN_AVAILABLE = 3,
    CREATURE_LOOT_STATUS_SKINNED        = 4
};

enum LootMethod
{
    FREE_FOR_ALL                        = 0,
    ROUND_ROBIN                         = 1,
    MASTER_LOOT                         = 2,
    GROUP_LOOT                          = 3,
    NEED_BEFORE_GREED                   = 4,

    NOT_GROUP_TYPE_LOOT                 = 5                                 // internal use only
};

// internal loot type
enum LootType
{
    LOOT_NONE                           = 0,
    LOOT_CORPSE                         = 1,
    LOOT_PICKPOCKETING                  = 2,
    LOOT_FISHING                        = 3,
    LOOT_DISENCHANTING                  = 4,
    LOOT_ITEM                           = 5,
    LOOT_SKINNING                       = 6,
    LOOT_PROSPECTING                    = 7,
    LOOT_MILLING                        = 8,
    LOOT_FISHINGHOLE                    = 20,
    LOOT_FISHING_FAIL                   = 21,
    LOOT_INSIGNIA                       = 22,
    LOOT_MAIL                           = 23,
    LOOT_SPELL                          = 24,

    LOOT_DEBUG                          = 100
};

enum LootSlotType
{
    LOOT_SLOT_NORMAL                    = 0,                // can be looted
    LOOT_SLOT_VIEW                      = 1,                // can be only view (ignore any loot attempts)
    LOOT_SLOT_MASTER                    = 2,                // can be looted only master (error message)
    LOOT_SLOT_REQS                      = 3,                // can't be looted (error message about missing reqs)
    LOOT_SLOT_OWNER                     = 4,                // ignore binding confirmation and etc, for single player looting
    MAX_LOOT_SLOT_TYPE                                      // custom, use for mark skipped from show items
};

enum RollVote
{
    ROLL_PASS                           = 0,
    ROLL_NEED                           = 1,
    ROLL_GREED                          = 2,
    ROLL_DISENCHANT                     = 3,
    ROLL_NOT_EMITED_YET                 = 4,                // send to client
    ROLL_NOT_VALID                      = 5                 // not send to client
};

                                                            // set what votes allowed
enum RollVoteMask
{
    ROLL_VOTE_MASK_PASS                 = 0x01,
    ROLL_VOTE_MASK_NEED                 = 0x02,
    ROLL_VOTE_MASK_GREED                = 0x04,
    ROLL_VOTE_MASK_DISENCHANT           = 0x08,

    ROLL_VOTE_MASK_ALL                  = 0x0F,
};

enum LootItemType
{
    LOOTITEM_TYPE_NORMAL                = 1,
    LOOTITEM_TYPE_QUEST                 = 2,
    LOOTITEM_TYPE_CONDITIONNAL          = 3
};

                                                            // loot type sent to clients
enum ClientLootType
{
    CLIENT_LOOT_CORPSE                  = 1,
    CLIENT_LOOT_PICKPOCKETING           = 2,
    CLIENT_LOOT_FISHING                 = 3,
    CLIENT_LOOT_DISENCHANTING           = 4
};

enum LootStatus
{
    LOOT_STATUS_NOT_FULLY_LOOTED        = 0x01,
    LOOT_STATUS_CONTAIN_FFA             = 0x02,
    LOOT_STATUS_CONTAIN_GOLD            = 0x04,
    LOOT_STATUS_CONTAIN_RELEASED_ITEMS  = 0x08,
    LOOT_STATUS_ONGOING_ROLL            = 0x10,
    LOOT_STATUS_FAKE_LOOT               = 0x20
};

enum LootError
{
    LOOT_ERROR_DIDNT_KILL               = 0,                // You don't have permission to loot that corpse.
    LOOT_ERROR_TOO_FAR                  = 4,                // You are too far away to loot that corpse.
    LOOT_ERROR_BAD_FACING               = 5,                // You must be facing the corpse to loot it.
    LOOT_ERROR_LOCKED                   = 6,                // Someone is already looting that corpse.
    LOOT_ERROR_NOTSTANDING              = 8,                // You need to be standing up to loot something!
    LOOT_ERROR_STUNNED                  = 9,                // You can't loot anything while stunned!
    LOOT_ERROR_PLAYER_NOT_FOUND         = 10,               // Player not found
    LOOT_ERROR_PLAY_TIME_EXCEEDED       = 11,               // Maximum play time exceeded
    LOOT_ERROR_MASTER_INV_FULL          = 12,               // That player's inventory is full
    LOOT_ERROR_MASTER_UNIQUE_ITEM       = 13,               // Player has too many of that item already
    LOOT_ERROR_MASTER_OTHER             = 14,               // Can't assign item to that player
    LOOT_ERROR_ALREADY_PICKPOCKETED     = 15,               // Your target has already had its pockets picked
    LOOT_ERROR_NOT_WHILE_SHAPESHIFTED   = 16                // You can't do that while shapeshifted.
};

class Loot;
class LootBase;

typedef std::unique_ptr<LootBase> LootBaseUPtr;


#endif
