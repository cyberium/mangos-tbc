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

#include "LootItem.h"
#include "LootStore.h"
#include "Globals/ObjectMgr.h"
#include "Entities/ItemEnchantmentMgr.h"
#include "World/World.h"

// Constructor, copies most fields from LootStoreItem and generates random count
LootItem::LootItem(LootStoreItem const& li, uint32 _lootSlot, uint32 threshold)
{
    if (li.needs_quest)
        lootItemType = LOOTITEM_TYPE_QUEST;
    else if (li.conditionId)
        lootItemType = LOOTITEM_TYPE_CONDITIONNAL;
    else
        lootItemType = LOOTITEM_TYPE_NORMAL;

    itemProto = ObjectMgr::GetItemPrototype(li.itemid);
    if (itemProto)
    {
        freeForAll = (itemProto->Flags & ITEM_FLAG_MULTI_DROP) != 0;
        displayID = itemProto->DisplayInfoID;
        isUnderThreshold = itemProto->Quality < threshold;
    }
    else
    {
        sLog.outError("LootItem::LootItem()> item ID(%u) have no prototype!", li.itemid); // maybe i must make an assert here
        freeForAll = false;
        displayID = 0;
        isUnderThreshold = false;
    }


    itemId = li.itemid;
    conditionId = li.conditionId;
    lootSlot = _lootSlot;
    count = urand(li.mincountOrRef, li.maxcount);     // constructor called for mincountOrRef > 0 only
    randomSuffix = GenerateEnchSuffixFactor(itemId);
    randomPropertyId = Item::GenerateItemRandomPropertyId(itemId);
    isBlocked = false;
    currentLooterPass = false;
    isReleased = false;
}

LootItem::LootItem(uint32 _itemId, uint32 _count, uint32 _randomSuffix, int32 _randomPropertyId, uint32 _lootSlot)
{
    itemProto = ObjectMgr::GetItemPrototype(_itemId);
    if (itemProto)
    {
        freeForAll = (itemProto->Flags & ITEM_FLAG_MULTI_DROP) != 0;
        displayID = itemProto->DisplayInfoID;
    }
    else
    {
        sLog.outError("LootItem::LootItem()> item ID(%u) have no prototype!", _itemId); // maybe i must make an assert here
        freeForAll = false;
        displayID = 0;
    }

    itemId = _itemId;
    lootSlot = _lootSlot;
    conditionId = 0;
    lootItemType = LOOTITEM_TYPE_NORMAL;
    count = _count;
    randomSuffix = _randomSuffix;
    randomPropertyId = _randomPropertyId;
    isBlocked = false;
    isUnderThreshold = false;
    currentLooterPass = false;
    isReleased = false;
}


LootItem::LootItem(LootStoreItem const& li, uint32 _lootSlot)
{
    if (li.needs_quest)
        lootItemType = LOOTITEM_TYPE_QUEST;
    else if (li.conditionId)
        lootItemType = LOOTITEM_TYPE_CONDITIONNAL;
    else
        lootItemType = LOOTITEM_TYPE_NORMAL;

    itemProto = ObjectMgr::GetItemPrototype(li.itemid);
    if (itemProto)
    {
        freeForAll = (itemProto->Flags & ITEM_FLAG_MULTI_DROP) != 0;
        displayID = itemProto->DisplayInfoID;
    }
    else
    {
        sLog.outError("LootItem::LootItem()> item ID(%u) have no prototype!", li.itemid); // maybe i must make an assert here
        freeForAll = false;
        displayID = 0;
    }

    itemId = li.itemid;
    conditionId = li.conditionId;
    lootSlot = _lootSlot;
    count = urand(li.mincountOrRef, li.maxcount);     // constructor called for mincountOrRef > 0 only
    randomSuffix = GenerateEnchSuffixFactor(itemId);
    randomPropertyId = Item::GenerateItemRandomPropertyId(itemId);
    isBlocked = false;
    currentLooterPass = false;
    isReleased = false;
    isUnderThreshold = false;
}

// Basic checks for player/item compatibility - if false no chance to see the item in the loot
bool LootItem::AllowedForPlayer(Player const* player, Object const* lootTarget) const
{
    if (!itemProto)
        return false;

    switch (lootItemType)
    {
        case LOOTITEM_TYPE_NORMAL:
            break;

        case LOOTITEM_TYPE_CONDITIONNAL:
            // DB conditions check
            if (lootTarget && (lootTarget->IsUnit() || lootTarget->IsGameObject()))
            {
                // Add a bit of time to let player pickup item if despawn timer is near
                auto target = static_cast<WorldObject const*>(lootTarget);
                if (!sObjectMgr.IsConditionSatisfied(conditionId, player, player->GetMap(), target, CONDITION_FROM_LOOT))
                    return false;
            }
            else
            {
                sLog.outError("%s have conditions id(%u) from loot not yet implemented!", lootTarget->GetGuidStr().c_str(), conditionId);
                return false;
            }
            break;

        case LOOTITEM_TYPE_QUEST:
            // Checking quests for quest-only drop (check only quests requirements in this case)
            if (!player->HasQuestForItem(itemId))
                return false;
            break;

        default:
            break;
    }

    // Not quest only drop (check quest starting items for already accepted non-repeatable quests)
    if (itemProto->StartQuest && player->GetQuestStatus(itemProto->StartQuest) != QUEST_STATUS_NONE && !player->HasQuestForItem(itemId))
        return false;

    return true;
}

LootSlotType LootItem::GetSlotTypeForSharedLoot(Player const* player, Loot const* loot) const
{
    // special case with master loot can be able to see conditional item even if he don't have prerequisite
    if (loot->m_lootMethod == MASTER_LOOT && lootItemType == LOOTITEM_TYPE_CONDITIONNAL)
    {
        if (isUnderThreshold)
        {
            if (!IsAllowed(player, loot))
                return MAX_LOOT_SLOT_TYPE;

            if (isBlocked)
                return LOOT_SLOT_VIEW;

            if (isReleased || currentLooterPass || loot->m_currentLooterGuid == player->GetObjectGuid())
                return LOOT_SLOT_OWNER;

            return MAX_LOOT_SLOT_TYPE;
        }

        if (player->GetObjectGuid() == loot->m_masterOwnerGuid && allowedGuid.size() > 0)
            return LOOT_SLOT_MASTER;

        if (!IsAllowed(player, loot))
            return MAX_LOOT_SLOT_TYPE;

        if (isReleased || currentLooterPass || loot->m_currentLooterGuid == player->GetObjectGuid())
            return LOOT_SLOT_OWNER;

        if (sWorld.getConfig(CONFIG_BOOL_CORPSE_ALLOW_ALL_ITEMS_SHOW_IN_MASTER_LOOT))
            return LOOT_SLOT_VIEW;

        return MAX_LOOT_SLOT_TYPE;
    }

    // Check if still have right to pick this item
    if (!IsAllowed(player, loot))
        return MAX_LOOT_SLOT_TYPE;

    if (freeForAll)
        return loot->m_lootMethod == NOT_GROUP_TYPE_LOOT ? LOOT_SLOT_OWNER : LOOT_SLOT_NORMAL; // player have not yet looted a free for all item

    // quest items and conditional items cases
    if (lootItemType == LOOTITEM_TYPE_QUEST || lootItemType == LOOTITEM_TYPE_CONDITIONNAL)
    {
        switch (loot->m_lootMethod)
        {
            case NOT_GROUP_TYPE_LOOT:
            case FREE_FOR_ALL:
                return LOOT_SLOT_OWNER;

            default:
                if (loot->m_isChest)
                    return LOOT_SLOT_OWNER;

                if (isBlocked)
                    return LOOT_SLOT_VIEW;

                // Check if its turn of that player to loot a not party loot. The loot may be released or the item may be passed by currentLooter
                if (isReleased || currentLooterPass || loot->m_currentLooterGuid == player->GetObjectGuid())
                    return LOOT_SLOT_OWNER;
                return MAX_LOOT_SLOT_TYPE;
        }
    }

    switch (loot->m_lootMethod)
    {
        case FREE_FOR_ALL:
            return LOOT_SLOT_OWNER;
        case GROUP_LOOT:
        case NEED_BEFORE_GREED:
        {
            if (!isBlocked)
            {
                if (loot->m_isChest)
                    return LOOT_SLOT_NORMAL;

                if (isReleased || currentLooterPass || player->GetObjectGuid() == loot->m_currentLooterGuid)
                    return LOOT_SLOT_NORMAL;

                return MAX_LOOT_SLOT_TYPE;
            }
            return LOOT_SLOT_VIEW;
        }
        case MASTER_LOOT:
        {
            if (isUnderThreshold)
            {
                if (loot->m_isChest)
                    return LOOT_SLOT_OWNER;

                if (isBlocked)
                    return LOOT_SLOT_VIEW;

                if (isReleased || currentLooterPass || player->GetObjectGuid() == loot->m_currentLooterGuid)
                    return LOOT_SLOT_OWNER;

                return MAX_LOOT_SLOT_TYPE;
            }

            if (player->GetObjectGuid() == loot->m_masterOwnerGuid)
                return LOOT_SLOT_MASTER;

            // give a chance to let others just see the content of the loot
            if (isBlocked || sWorld.getConfig(CONFIG_BOOL_CORPSE_ALLOW_ALL_ITEMS_SHOW_IN_MASTER_LOOT))
                return LOOT_SLOT_VIEW;

            return MAX_LOOT_SLOT_TYPE;
        }
        case ROUND_ROBIN:
        {
            if (loot->m_isChest)
                return LOOT_SLOT_NORMAL;

            if (isReleased || currentLooterPass || player->GetObjectGuid() == loot->m_currentLooterGuid)
                return LOOT_SLOT_OWNER;

            return MAX_LOOT_SLOT_TYPE;
        }
        case NOT_GROUP_TYPE_LOOT:
            return LOOT_SLOT_OWNER;
        default:
            return MAX_LOOT_SLOT_TYPE;
    }
}

std::string LootItem::ToString() const
{
    std::stringstream ss;

    ss << "id(" << itemId << "), slot(" << lootSlot << "), allowed(" << allowedGuid.size() << "), condition(" << conditionId << ")";
    return ss.str();
}