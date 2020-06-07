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

#ifndef CMANGOS_LOOT_ITEM_H
#define CMANGOS_LOOT_ITEM_H

#include "Common.h"
#include "LootDefines.h"
#include "Entities/ObjectGuid.h"
#include "Entities/ItemPrototype.h"
#include "ByteBuffer.h"

class Object;
class Player;
class WorldObject;
struct LootStoreItem;

struct LootItem
{
    uint32       itemId;
    uint32       randomSuffix;
    int32        randomPropertyId;
    uint32       displayID;
    LootItemType lootItemType;
    GuidSet      allowedGuid;                                       // player's that have right to loot this item
    GuidSet      pickedUpGuid;                                      // player's that have already picked the item
    uint32       lootSlot;                                          // the slot number will be send to client
    uint16       conditionId       : 16;                            // allow compiler pack structure
    uint8        count             : 8;
    bool         isBlocked         : 1;
    bool         freeForAll        : 1;                             // free for all
    bool         isUnderThreshold  : 1;
    bool         currentLooterPass : 1;
    bool         isReleased        : 1;                             // true if item is released by looter or by roll system

    // storing item prototype for fast access
    ItemPrototype const* itemProto;

    // Constructor, copies most fields from LootStoreItem, generates random count and random suffixes/properties
    // Should be called for non-reference LootStoreItem entries only (mincountOrRef > 0)
    explicit LootItem(LootStoreItem const& li, uint32 _lootSlot, uint32 threshold);
    explicit LootItem(LootStoreItem const& li, uint32 _lootSlot);

    LootItem(uint32 _itemId, uint32 _count, uint32 _randomSuffix, int32 _randomPropertyId, uint32 _lootSlot);

    // Basic checks for player/item compatibility - if false no chance to see the item in the loot
    bool AllowedForPlayer(Player const* player, Object const* lootTarget) const;
    LootSlotType GetSlotTypeForSharedLoot(Player const* player, Loot const* loot) const;
    bool IsAllowed(Player const* player, Loot const* loot) const;

    std::string ToString() const;
};


typedef std::shared_ptr<LootItem> LootItemSPtr;
typedef std::vector<LootItemSPtr> LootItemVec;
typedef std::shared_ptr<LootItemVec> LootItemVecSPtr;
typedef std::unique_ptr<LootItemVec> LootItemVecUPtr;


ByteBuffer& operator<<(ByteBuffer& b, LootItem const& li);

#endif