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

#include "Loot.h"
#include "LootRules.h"
#include "LootStore.h"
#include "Globals/ObjectAccessor.h"
#include "Entities/GameObject.h"
#include "World/World.h"

//////////////////////////////////////////////////////////////////////////
// Base class of loot rule
//////////////////////////////////////////////////////////////////////////

void LootRule::Initialize(Player& player)
{
    m_ownerSet.emplace(player.GetObjectGuid());
};

bool LootRule::CanLootSlot(ObjectGuid const& targetGuid, uint32 itemSlot)
{
    auto lootItem = GetLootItemInSlot(itemSlot);
    if (!lootItem)
        return false;

    if (lootItem->freeForAll && lootItem->pickedUpGuid.find(targetGuid) == lootItem->pickedUpGuid.end())
        return true;

    if (lootItem->pickedUpGuid.find(targetGuid) == lootItem->pickedUpGuid.end())
        return true;
    return false;
}

bool LootRule::IsLootedForAll() const
{
    for (auto guid : m_ownerSet)
    {
        Player* plr = ObjectAccessor::FindPlayer(guid);
        if (plr && HaveItemFor(*plr))
            return false;
    }

    return true;
}

void LootRule::Reset()
{
    m_lootItems->clear();
}

bool LootRule::FillLoot(uint32 lootId, LootStore const& store, bool noEmptyError /*= false*/)
{
    LootTemplate const* tab = store.GetLootFor(lootId);

    if (tab)
    {
        tab->Process(m_loot, store); // Processing is done there, callback via Loot::AddItem()
        return true;
    }

    if (!noEmptyError)
        sLog.outErrorDb("Table '%s' loot id #%u used but it doesn't have records.", store.GetName(), lootId);

    return false;
}

bool LootRule::AddItem(LootStoreItem const& lootStoreItem)
{
    if (m_lootItems->size() < MAX_NR_LOOT_ITEMS)
    {
        m_lootItems->emplace_back(new LootItem(lootStoreItem, m_lootItems->size()));

        for (auto owner : m_ownerSet)
        {
            Player* plr = ObjectAccessor::FindPlayer(owner);
            if (plr && m_lootItems->back()->AllowedForPlayer(plr, m_loot.GetLootTarget()))
                m_lootItems->back()->allowedGuid.emplace(owner);
        }
        return true;
    }

    return false;
}

void LootRule::AddSavedItem(uint32 itemid, uint32 count, uint32 randomSuffix, int32 randomPropertyId)
{
    if (m_lootItems->size() < MAX_NR_LOOT_ITEMS)
    {
        m_lootItems->emplace_back(new LootItem(itemid, count, randomSuffix, randomPropertyId, m_lootItems->size()));

        // we consider here that all saved item are allowed for all owners
        // wrong if there is more than one player that have access to this item loot
        // TODO:: check if that case exist and thus manage pickup case
        for (auto owner : m_ownerSet)
            m_lootItems->back()->allowedGuid.emplace(owner);
    }
}

// will return the pointer of item in loot slot provided without any right check
LootItemSPtr LootRule::GetLootItemInSlot(uint32 itemSlot)
{
    for (auto lootItem : *m_lootItems)
    {
        if (lootItem->lootSlot == itemSlot)
            return lootItem;
    }
    return nullptr;
}

void LootRule::OnRelease(Player& plr)
{
    m_playersLooting.erase(plr.GetObjectGuid());
}

void LootRule::OnPlayerLooting(Player& plr)
{
    m_playersLooting.insert(plr.GetObjectGuid());
}

void LootRule::SetItemSent(LootItemSPtr lootItem, Player* player)
{
    auto storedItemItr = std::find(m_lootItems->begin(), m_lootItems->end(), lootItem);
    if (storedItemItr != m_lootItems->end())
        (*storedItemItr)->pickedUpGuid.insert(player->GetObjectGuid());
}

bool LootRule::IsItemAlreadyIn(uint32 itemId) const
{
    for (auto lootItem : *m_lootItems)
    {
        if (lootItem->itemId == itemId)
            return true;
    }
    return false;
}

void LootRule::GenerateMoneyLoot(uint32 minAmount, uint32 maxAmount)
{
    if (maxAmount > 0)
    {
        if (maxAmount <= minAmount)
            m_gold = uint32(maxAmount * sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_MONEY));
        else if ((maxAmount - minAmount) < 32700)
            m_gold = uint32(urand(minAmount, maxAmount) * sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_MONEY));
        else
            m_gold = uint32(urand(minAmount >> 8, maxAmount >> 8)* sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_MONEY)) << 8;
    }
}

bool LootRule::IsEligibleForLoot(Player const& looter, LootBase const& loot)
{
    MANGOS_ASSERT(loot.GetLootTarget()->isType(TYPEMASK_WORLDOBJECT));

    auto lootTarget = static_cast<WorldObject*>(loot.GetLootTarget());

    // check distance
    if (!looter.IsAtGroupRewardDistance(lootTarget))
        return false;

    /*if (lootTarget->IsUnit())
    {
        Unit* creature = static_cast<Unit*>(lootTarget);

        //TODO::HasThreat should use const pointer to remove following hack
        Player* player = const_cast<Player*>(&looter);      //hack

        return creature->getThreatManager().HasThreat(player);
    }*/

    return true;
}

//////////////////////////////////////////////////////////////////////////
// Skinning rule
//////////////////////////////////////////////////////////////////////////

bool SkinningRule::IsLootedForAll() const
{
    for (auto& lootItem : *m_lootItems)
    {
        if (lootItem->pickedUpGuid.size() == 0)
            return false;
    }
    return true;
}

void SkinningRule::OnRelease(Player& plr)
{
    if (HaveItemFor(plr))
    {
        m_isReleased = true;
    }
    LootRule::OnRelease(plr);
}

// check if provided player have some items right
// if container is provided fill it with authorized items
bool SkinningRule::HaveItemFor(Player const& player, LootItemRightVec* lootItems /*= nullptr*/) const
{
    if (lootItems)
        lootItems->clear();

    ObjectGuid pGuid = player.GetObjectGuid();
    if (!m_isReleased && m_ownerSet.find(pGuid) == m_ownerSet.end())
        return false;

    for (auto& lootItem : *m_lootItems)
    {
        // item already picked by this player?
        if (lootItem->pickedUpGuid.find(pGuid) != lootItem->pickedUpGuid.end())
            continue;

        // permission is computed at loot time as this can happen for
        // any player not only player in the group
        if (!lootItem->AllowedForPlayer(&player, m_loot.GetLootTarget()))
            continue;

        // if no container provided we can return true here
        if (!lootItems)
            return true;

        lootItems->emplace_back(lootItem, LOOT_SLOT_OWNER);
    }

    return lootItems ? !lootItems->empty() : false;
}

//////////////////////////////////////////////////////////////////////////
// Single player rule
//////////////////////////////////////////////////////////////////////////

bool SinglePlayerRule::HaveItemFor(Player const& player, LootItemRightVec* lootItems /*= nullptr*/) const
{
    ObjectGuid pGuid = player.GetObjectGuid();
    if (m_ownerSet.find(pGuid) == m_ownerSet.end())
        return false;

    if (lootItems)
    {
        lootItems->clear();
        lootItems->reserve(m_lootItems->size());
    }

    for (auto& lootItem : *m_lootItems)
    {
        // item already picked by this player?
        if (lootItem->pickedUpGuid.find(pGuid) != lootItem->pickedUpGuid.end())
            continue;

        // is this player allowed to loot this item?
        if (!lootItem->IsAllowed(pGuid))
            continue;

        // if no container provided we can return true here
        if (!lootItems)
            return true;

        lootItems->emplace_back(lootItem, LOOT_SLOT_OWNER);
    }

    return lootItems ? !lootItems->empty() : false;
}

//////////////////////////////////////////////////////////////////////////
// Chest rule for single player
// Any player can see what is inside and loot it
//////////////////////////////////////////////////////////////////////////

void ChestSinglePlayerRule::Initialize(Player& player)
{
    MANGOS_ASSERT(m_loot.GetLootTarget()->IsGameObject());

    auto gob = static_cast<GameObject*>(m_loot.GetLootTarget());
    auto goInfo = gob->GetGOInfo();

    MANGOS_ASSERT(goInfo->type == GAMEOBJECT_TYPE_CHEST);
}

bool ChestSinglePlayerRule::IsEmpty() const
{
    if (LootRule::IsEmpty())
        return true;

    for (auto lootItem : *m_lootItems)
    {
        /*if (lootItem->freeForAll)
            return false;*/

        if (lootItem->pickedUpGuid.empty())
            return false;
    }

    return true;
}

bool ChestSinglePlayerRule::HaveItemFor(Player const& player, LootItemRightVec* lootItems /*= nullptr*/) const
{
    ObjectGuid pGuid = player.GetObjectGuid();
    if (m_playersLooting.empty())
        return false;

    if (lootItems)
    {
        lootItems->clear();
        lootItems->reserve(m_lootItems->size());
    }

    for (auto& lootItem : *m_lootItems)
    {
        // item already picked by this player?
        if (lootItem->pickedUpGuid.find(pGuid) != lootItem->pickedUpGuid.end())
            continue;

        // only quest items can be looted more than one time
        if (!lootItem->freeForAll && !lootItem->pickedUpGuid.empty())
            continue;

        // permission is computed at loot time as this can happen for
        // any player not only player in the group
        if (!lootItem->AllowedForPlayer(&player, m_loot.GetLootTarget()))
            continue;

        // if no container provided we can return true here
        if (!lootItems)
            return true;

        lootItems->emplace_back(lootItem, LOOT_SLOT_OWNER);
    }

    return lootItems ? !lootItems->empty() : false;
}

void ChestSinglePlayerRule::OnRelease(Player& plr)
{
    LootRule::OnRelease(plr);
}


//////////////////////////////////////////////////////////////////////////
// Chest rule
//////////////////////////////////////////////////////////////////////////

void FreeForAllRule::Initialize(Player& player)
{
    Group* grp = player.GetGroup();
    if (grp)
    {
        // we need to fill m_ownerSet with player who have access to the loot
        Group::MemberSlotList const& memberList = grp->GetMemberSlots();
        ObjectGuid currentLooterGuid = grp->GetCurrentLooterGuid();
        GuidVector ownerList;                               // used to keep order of the player (important to get correctly next looter)

        auto currentLooterItr = grp->GetMemberSlot(currentLooterGuid);
        // current looter must be in the group
        // if not then assign first player in group
        if (currentLooterItr == memberList.end())
        {
            currentLooterItr = memberList.begin();
            currentLooterGuid = currentLooterItr->guid;
            grp->SetNextLooterGuid(currentLooterGuid);
        }

        // now that we get a valid current looter iterator we can start to check the loot owner
        auto itr = currentLooterItr;
        do
        {
            ++itr;                                          // we start by next current looter position in list that way we have directly next looter in result ownerSet (if any exist)
            if (itr == memberList.end())
                itr = memberList.begin();                   // reached the end of the list is possible so simply restart from the first element in it

            Player* looter = ObjectAccessor::FindPlayer(itr->guid);

            if (!looter)
                continue;

            if (IsEligibleForLoot(*looter, m_loot))
            {
                m_ownerSet.insert(itr->guid);               // save this guid to main owner set
                ownerList.push_back(itr->guid);             // save this guid to local ordered GuidList (we need to keep it ordered only here)
            }

        } while (itr != currentLooterItr);
    }
}

bool FreeForAllRule::HaveItemFor(Player const& player, LootItemRightVec* lootItems /*= nullptr*/) const
{
    ObjectGuid pGuid = player.GetObjectGuid();
    if (m_ownerSet.find(pGuid) == m_ownerSet.end())
        return false;

    if (lootItems)
    {
        lootItems->clear();
        lootItems->reserve(m_lootItems->size());
    }

    for (auto& lootItem : *m_lootItems)
    {
        // item already picked by this player?
        if (lootItem->pickedUpGuid.find(pGuid) != lootItem->pickedUpGuid.end())
            continue;

        // some quest loot can be available for all group members
        if (!lootItem->freeForAll && !lootItem->pickedUpGuid.empty())
            continue;

        LootSlotType slotType = LOOT_SLOT_NORMAL;
        // is this player allowed to loot this item?
        if (!lootItem->IsAllowed(pGuid))
        {
            if (lootItem->lootItemType != LOOTITEM_TYPE_CONDITIONNAL)
                continue;

            if (!lootItem->pickedUpGuid.empty())
                continue;

            slotType = LOOT_SLOT_VIEW;
        }

        // if no container provided we can return true here
        if (!lootItems)
            return true;

        lootItems->emplace_back(lootItem, slotType);
    }

    return lootItems ? !lootItems->empty() : false;
}

void GroupLootRule::Initialize(Player& player)
{
    Group* grp = player.GetGroup();
    if (grp)
    {
        m_threshold = grp->GetLootThreshold();

        // we need to fill m_ownerSet with player who have access to the loot
        Group::MemberSlotList const& memberList = grp->GetMemberSlots();
        ObjectGuid currentLooterGuid = grp->GetCurrentLooterGuid();
        GuidVector ownerList;                               // used to keep order of the player (important to get correctly next looter)

        auto currentLooterItr = grp->GetMemberSlot(currentLooterGuid);
        // current looter must be in the group
        // if not then assign first player in group
        if (currentLooterItr == memberList.end())
        {
            currentLooterItr = memberList.begin();
            currentLooterGuid = currentLooterItr->guid;
            grp->SetNextLooterGuid(currentLooterGuid);
        }

        // now that we get a valid current looter iterator we can start to check the loot owner
        auto itr = currentLooterItr;
        do
        {
            ++itr;                                          // we start by next current looter position in list that way we have directly next looter in result ownerSet (if any exist)
            if (itr == memberList.end())
                itr = memberList.begin();                   // reached the end of the list is possible so simply restart from the first element in it

            Player* looter = ObjectAccessor::FindPlayer(itr->guid);

            if (!looter)
                continue;

            if (IsEligibleForLoot(*looter, m_loot))
            {
                m_ownerSet.insert(itr->guid);               // save this guid to main owner set
                ownerList.push_back(itr->guid);             // save this guid to local ordered GuidList (we need to keep it ordered only here)
            }

        } while (itr != currentLooterItr);

        // if more than one player have right to loot than we have to handle group method, round robin, roll, etc..
        if (m_ownerSet.size() > 1)
        {
            // is current looter have right for this loot?
            if (m_ownerSet.find(currentLooterGuid) == m_ownerSet.end())
            {
                // as owner list is filled from NEXT current looter position we can assign first element in list as looter and next element in list as next looter
                m_currentLooterGuid = ownerList.front();        // set first player who have right to loot as current looter
                grp->SetNextLooterGuid(*(++ownerList.begin())); // set second player who have right as next looter
            }
            else
            {
                // as owner set is filled from NEXT current looter position we can assign first element in list as next looter
                m_currentLooterGuid = currentLooterGuid;
                grp->SetNextLooterGuid(ownerList.front());      // set first player who have right as next looter
            }
        }
        else
            m_currentLooterGuid = player.GetObjectGuid();

        SendAllowedLooter();
    }
}

bool GroupLootRule::AddItem(LootStoreItem const& lootStoreItem)
{
    if (m_lootItems->size() < MAX_NR_LOOT_ITEMS)
    {
        m_lootItems->emplace_back(new LootItem(lootStoreItem, m_lootItems->size(), uint32(m_threshold)));

        for (auto owner : m_ownerSet)
        {
            Player* plr = ObjectAccessor::FindPlayer(owner);
            if (plr && m_lootItems->back()->AllowedForPlayer(plr, m_loot.GetLootTarget()))
                m_lootItems->back()->allowedGuid.emplace(owner);
        }

        // if at least one player have right to loot it
        if (m_lootItems->back()->allowedGuid.size() > 1)
        {
            if (!m_lootItems->back()->isUnderThreshold)
                m_lootItems->back()->isBlocked = true;
        }

        return true;
    }

    return false;
}

bool GroupLootRule::HaveItemFor(Player const& player, LootItemRightVec* lootItems /*= nullptr*/) const
{
    ObjectGuid pGuid = player.GetObjectGuid();
    if (m_ownerSet.find(pGuid) == m_ownerSet.end())
        return false;

    if (lootItems)
    {
        lootItems->clear();
        lootItems->reserve(m_lootItems->size());
    }

    for (auto& lootItem : *m_lootItems)
    {
        // item already picked by this player?
        if (lootItem->pickedUpGuid.find(pGuid) != lootItem->pickedUpGuid.end())
            continue;

        // some quest loot can be available for all group members
        if (!lootItem->freeForAll && !lootItem->pickedUpGuid.empty())
            continue;

        LootSlotType slotType = LOOT_SLOT_NORMAL;
        // is this player allowed to loot this item?
        if (!lootItem->IsAllowed(pGuid))
        {
            if (lootItem->lootItemType != LOOTITEM_TYPE_CONDITIONNAL)
                continue;

            if (!lootItem->pickedUpGuid.empty())
                continue;

            slotType = LOOT_SLOT_REQS;
        }

        if (lootItem->isUnderThreshold)
        {
            // if current looter have not right to this item permit to anyone that have right to loot it
            if (lootItem->allowedGuid.find(m_currentLooterGuid) != lootItem->allowedGuid.end())
            {
                if (!m_currentLooterReleased && m_currentLooterGuid != pGuid)
                    continue;
            }
        }
        else
        {
            if (lootItem->isBlocked && slotType != LOOT_SLOT_REQS)
            {
                slotType = LOOT_SLOT_VIEW;
            }
        }

        // if no container provided we can return true here
        if (!lootItems)
            return true;

        lootItems->emplace_back(lootItem, slotType);
    }

    return lootItems ? !lootItems->empty() : false;
}

void GroupLootRule::OnRelease(Player& plr)
{
    if (plr.GetObjectGuid() == m_currentLooterGuid)
        m_currentLooterReleased = true;
    LootRule::OnRelease(plr);
}

void GroupLootRule::OnPlayerLooting(Player& plr)
{
    LootRule::OnPlayerLooting(plr);
    if (!m_rollChecked)
    {
        m_rollChecked = true;

        // check if there is need to launch a roll
        for (auto lootItem : *m_lootItems)
        {
            if (!lootItem->isBlocked)
                continue;

            uint32 itemSlot = lootItem->lootSlot;

            if (m_rolls.find(itemSlot) == m_rolls.end() && lootItem->IsAllowed(plr.GetObjectGuid()))
            {
                if (!m_rolls[itemSlot].TryToStart(m_loot, lootItem))    // Create and try to start a roll
                    m_rolls.erase(m_rolls.find(itemSlot));              // Cannot start roll so we have to delete it (find will not fail as the item was just created)
            }
        }
    }
}

GroupLootRoll* GroupLootRule::GetRollForSlot(uint32 itemSlot)
{
    GroupLootRollMap::iterator rollItr = m_rolls.find(itemSlot);
    if (rollItr == m_rolls.end())
        return nullptr;
    return &rollItr->second;
}

void GroupLootRule::Update(uint32 diff)
{
    GroupLootRollMap::iterator itr = m_rolls.begin();
    while (itr != m_rolls.end())
    {
        if (itr->second.UpdateRoll(diff))
            m_rolls.erase(itr++);
        else
            ++itr;
    }
}

void GroupLootRule::SendAllowedLooter()
{
    auto lootTarget = static_cast<WorldObject*>(m_loot.GetLootTarget());
    if (!lootTarget->IsInWorld())
        return;

    Map* lootTargetMap = lootTarget->GetMap();

    WorldPacket data(SMSG_LOOT_LIST);
    data << m_loot.GetLootTarget()->GetObjectGuid();
    data << uint8(0);
    data << m_currentLooterGuid.WriteAsPacked();

    for (auto itr : m_ownerSet)
    {
        if (Player* plr = ObjectAccessor::FindPlayer(itr))
        {
            if (plr->IsInWorld() && plr->GetMap() == lootTargetMap)
            {
                if (WorldSession* session = plr->GetSession())
                    session->SendPacket(data);
            }
        }
    }
}

bool RoundRobinRule::HaveItemFor(Player const& player, LootItemRightVec* lootItems /*= nullptr*/) const
{
    ObjectGuid pGuid = player.GetObjectGuid();
    if (m_ownerSet.find(pGuid) == m_ownerSet.end())
        return false;

    if (lootItems)
    {
        lootItems->clear();
        lootItems->reserve(m_lootItems->size());
    }

    for (auto& lootItem : *m_lootItems)
    {
        // item already picked by this player?
        if (lootItem->pickedUpGuid.find(pGuid) != lootItem->pickedUpGuid.end())
            continue;

        // some quest loot can be available for all group members
        if (!lootItem->freeForAll && !lootItem->pickedUpGuid.empty())
            continue;

        LootSlotType slotType = LOOT_SLOT_NORMAL;
        // is this player allowed to loot this item?
        if (!lootItem->IsAllowed(pGuid))
        {
            if (lootItem->lootItemType != LOOTITEM_TYPE_CONDITIONNAL)
                continue;

            if (!lootItem->pickedUpGuid.empty())
                continue;

            slotType = LOOT_SLOT_REQS;
        }

        // if current looter have not right to this item permit to anyone that have right to loot it
        if (lootItem->allowedGuid.find(m_currentLooterGuid) != lootItem->allowedGuid.end())
        {
            if (!m_currentLooterReleased && m_currentLooterGuid != pGuid)
                continue;
        }

        // if no container provided we can return true here
        if (!lootItems)
            return true;

        lootItems->emplace_back(lootItem, slotType);
    }

    return lootItems ? !lootItems->empty() : false;
}

void RoundRobinRule::OnRelease(Player& plr)
{
    if (plr.GetObjectGuid() == m_currentLooterGuid)
        m_currentLooterReleased = true;
    LootRule::OnRelease(plr);
}

void MasterLootRule::Initialize(Player& player)
{
    GroupLootRule::Initialize(player);

    if (Group* grp = player.GetGroup())
        m_masterOwnerGuid = grp->GetMasterLooterGuid();
}

bool MasterLootRule::AddItem(LootStoreItem const& lootStoreItem)
{
    if (m_lootItems->size() < MAX_NR_LOOT_ITEMS)
    {
        m_lootItems->emplace_back(new LootItem(lootStoreItem, m_lootItems->size(), uint32(m_threshold)));

        for (auto owner : m_ownerSet)
        {
            Player* plr = ObjectAccessor::FindPlayer(owner);
            if (plr && m_lootItems->back()->AllowedForPlayer(plr, m_loot.GetLootTarget()))
                m_lootItems->back()->allowedGuid.emplace(owner);
        }

        // if at least one player have right to loot it
        if (m_lootItems->back()->allowedGuid.size() > 1)
        {
            if (!m_lootItems->back()->isUnderThreshold)
                m_lootItems->back()->isBlocked = true;
        }

        return true;
    }

    return false;
}

bool MasterLootRule::HaveItemFor(Player const& player, LootItemRightVec* lootItems /*= nullptr*/) const
{
    ObjectGuid pGuid = player.GetObjectGuid();
    if (m_ownerSet.find(pGuid) == m_ownerSet.end())
        return false;

    if (lootItems)
    {
        lootItems->clear();
        lootItems->reserve(m_lootItems->size());
    }

    for (auto& lootItem : *m_lootItems)
    {
        // item already picked by this player?
        if (lootItem->pickedUpGuid.find(pGuid) != lootItem->pickedUpGuid.end())
            continue;

        // some quest loot can be available for all group members
        if (!lootItem->freeForAll && !lootItem->pickedUpGuid.empty())
            continue;

        LootSlotType slotType = LOOT_SLOT_NORMAL;

        // is this player allowed to loot this item?
        if (!lootItem->IsAllowed(pGuid))
        {
            if (lootItem->lootItemType != LOOTITEM_TYPE_CONDITIONNAL)
                continue;

            if (!lootItem->pickedUpGuid.empty())
                continue;

            slotType = LOOT_SLOT_REQS;
        }

        if (!lootItem->freeForAll)
        {
            if (lootItem->isUnderThreshold)
            {
                // if current looter have not right to this item permit to anyone that have right to loot it
                if (lootItem->allowedGuid.find(m_currentLooterGuid) != lootItem->allowedGuid.end())
                {
                    if (!m_currentLooterReleased && m_currentLooterGuid != pGuid)
                        continue;
                }
            }
            else
            {
                if (pGuid == m_masterOwnerGuid && !lootItem->allowedGuid.empty())
                {
                    slotType = LOOT_SLOT_MASTER;
                }
                else
                {
                    if (slotType != LOOT_SLOT_REQS)
                        slotType = LOOT_SLOT_VIEW;
                }
            }

        }
        else
        {
            if (slotType != LOOT_SLOT_REQS)
                slotType = LOOT_SLOT_OWNER;
        }

        // if no container provided we can return true here
        if (!lootItems)
            return true;

        lootItems->emplace_back(lootItem, slotType);
    }

    return lootItems ? !lootItems->empty() : false;
}

void MasterLootRule::OnPlayerLooting(Player& plr)
{
    if (!m_rollChecked)
    {
        m_rollChecked = true;

        auto lootTarget = static_cast<WorldObject*>(m_loot.GetLootTarget());
        if (!lootTarget->IsInWorld())
            return;

        Map* lootTargetMap = lootTarget->GetMap();

        PlayerList playerList;
        Player* masterLooter = nullptr;
        for (auto playerGuid : m_ownerSet)
        {
            Player* player = sObjectAccessor.FindPlayer(playerGuid);
            if (!player || !player->IsInWorld() || player->GetMap() != lootTargetMap)
                continue;

            if (!player->GetSession())
                continue;

            if (!masterLooter && playerGuid == m_masterOwnerGuid)
                masterLooter = player;

            LootItemRightVec lootItemRight;
            if (HaveItemFor(*player, &lootItemRight))
            {
                for (auto lRight : lootItemRight)
                {
                    if (lRight.lootItem->isUnderThreshold)
                        continue;

                    playerList.emplace_back(player);
                    break;
                }
            }
        }

        // in master loot case we have to send looter list to client
        if (masterLooter)
        {
            WorldPacket data(SMSG_LOOT_MASTER_LIST);
            data << uint8(playerList.size());
            for (auto itr : playerList)
                data << itr->GetObjectGuid();
            masterLooter->GetSession()->SendPacket(data);
        }
    }
}

void MasterLootRule::SendAllowedLooter()
{
    auto lootTarget = static_cast<WorldObject*>(m_loot.GetLootTarget());
    if (!lootTarget->IsInWorld())
        return;

    Map* lootTargetMap = lootTarget->GetMap();

    WorldPacket data(SMSG_LOOT_LIST);
    data << m_loot.GetLootTarget()->GetObjectGuid();
    data << m_masterOwnerGuid.WriteAsPacked();
    data << m_currentLooterGuid.WriteAsPacked();

    for (auto itr : m_ownerSet)
    {
        if (Player* plr = ObjectAccessor::FindPlayer(itr))
        {
            if (plr->IsInWorld() && plr->GetMap() == lootTargetMap)
            {
                if (WorldSession* session = plr->GetSession())
                    session->SendPacket(data);
            }
        }
    }
}
