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

        for (auto owner : m_ownerSet)
        {
            Player* plr = ObjectAccessor::FindPlayer(owner);
            if (plr && m_lootItems->back()->AllowedForPlayer(plr, m_loot.GetLootTarget()))
                m_lootItems->back()->allowedGuid.emplace(owner);
        }
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
    if (CanLoot(plr))
    {
        m_isReleased = true;
    }
    LootRule::OnRelease(plr);
}

// check if provided player have some items right
// if container is provided fill it with authorized items
bool SkinningRule::HaveItemFor(Player const& player, LootItemRightVec* lootItems /*= nullptr*/) const
{
    ObjectGuid pGuid = player.GetObjectGuid();
    if (!m_isReleased && m_ownerSet.find(pGuid) == m_ownerSet.end())
        return false;

    if (lootItems)
        lootItems->clear();

    for (auto& lootItem : *m_lootItems)
    {
        // item already picked by this player?
        if (lootItem->pickedUpGuid.find(pGuid) != lootItem->pickedUpGuid.end())
            continue;

        // is this player allowed to loot this item?
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

}

void ChestSinglePlayerRule::OnRelease(Player& plr)
{

}


//////////////////////////////////////////////////////////////////////////
// Chest rule
//////////////////////////////////////////////////////////////////////////

/*
bool ChestRule::CanLoot(Player const& player) const
{

}

LootItemRightVecUPtr ChestRule::GetLootItem(Player const& player)
{

}

bool ChestRule::IsLootedFor(ObjectGuid const& targetGuid) const
{

}

void ChestRule::Initialize(Player& player)
{
    // generate loot only if ready for open and spawned in world
    if (m_lootItems->empty())
        return;

    m_ownerSet.clear();
    Group* grp = player.GetGroup();

    auto gob = static_cast<GameObject const*>(m_loot.GetLootTarget());

    if (grp && gob->GetGOInfo()->chest.groupLootRules)
    {
        auto lootMethod = grp->GetLootMethod();
        auto threshold = grp->GetLootThreshold();
        uint32 maxEnchantSkill = 0;
        // we need to fill m_ownerSet with player who have access to the loot
        Group::MemberSlotList const& memberList = grp->GetMemberSlots();
        ObjectGuid currentLooterGuid = grp->GetCurrentLooterGuid();
        GuidList ownerList;                 // used to keep order of the player (important to get correctly next looter)

        // current looter must be in the group
        Group::MemberSlotList::const_iterator currentLooterItr;
        for (currentLooterItr = memberList.begin(); currentLooterItr != memberList.end(); ++currentLooterItr)
            if (currentLooterItr->guid == currentLooterGuid)
                break;

        // if not then assign first player in group
        if (currentLooterItr == memberList.end())
        {
            currentLooterItr = memberList.begin();
            currentLooterGuid = currentLooterItr->guid;
            grp->SetNextLooterGuid(currentLooterGuid);
        }

        // now that we get a valid current looter iterator we can start to check the loot owner
        Group::MemberSlotList::const_iterator itr = currentLooterItr;
        do
        {
            ++itr;                              // we start by next current looter position in list that way we have directly next looter in result ownerSet (if any exist)
            if (itr == memberList.end())
                itr = memberList.begin();       // reached the end of the list is possible so simply restart from the first element in it

            Player* looter = ObjectAccessor::FindPlayer(itr->guid);

            if (!looter)
                continue;

            if (looter->IsAtGroupRewardDistance(m_loot.GetLootTarget()))
            {
                m_ownerSet.insert(itr->guid);   // save this guid to main owner set
                ownerList.push_back(itr->guid); // save this guid to local ordered GuidList (we need to keep it ordered only here)

                // get enchant skill of authorized looter
                uint32 enchantSkill = looter->GetSkillValue(SKILL_ENCHANTING);
                if (maxEnchantSkill < enchantSkill)
                    maxEnchantSkill = enchantSkill;
            }

        } while (itr != currentLooterItr);

        if (lootMethod == MASTER_LOOT)
        {
            m_masterOwnerGuid = grp->GetMasterLooterGuid();
            // Set group method to GROUP_LOOT if no master loot found
            if (m_ownerSet.find(m_masterOwnerGuid) == m_ownerSet.end())
            {
                lootMethod = GROUP_LOOT;
                m_masterOwnerGuid = ObjectGuid();
            }
        }

        // if more than one player have right to loot than we have to handle group method, round robin, roll, etc..
        if (m_ownerSet.size() > 1 && lootMethod != FREE_FOR_ALL)
        {
            // is current looter have right for this loot?
            if (m_ownerSet.find(currentLooterGuid) == m_ownerSet.end())
            {
                // as owner list is filled from NEXT current looter position we can assign first element in list as looter and next element in list as next looter
                m_currentLooterGuid = ownerList.front();                        // set first player who have right to loot as current looter
                grp->SetNextLooterGuid(*(++ownerList.begin()));                 // set second player who have right as next looter
            }
            else
            {
                // as owner set is filled from NEXT current looter position we can assign first element in list as next looter
                m_currentLooterGuid = currentLooterGuid;
                grp->SetNextLooterGuid(ownerList.front());                      // set first player who have right as next looter
            }

            SendAllowedLooter();
            m_isChecked = false;
            return;
        }
        m_currentLooterGuid = player->GetObjectGuid();
        SendAllowedLooter();
    }

    m_ownerSet.insert(player->GetObjectGuid());
}

void ChestRule::OnRelease(Player& plr)
{
    throw std::logic_error("The method or operation is not implemented.");
}

bool ChestRule::AddItem(LootStoreItem const& lootStoreItem)
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
}*/
