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

#include "Loot/LootMgr.h"
#include "Log.h"
#include "Globals/ObjectMgr.h"
#include "ProgressBar.h"
#include "World/World.h"
#include "Util.h"
#include "Globals/SharedDefines.h"
#include "Server/DBCStores.h"
#include "Server/SQLStorages.h"
#include "BattleGround/BattleGroundAV.h"
#include "Entities/ItemEnchantmentMgr.h"
#include "Tools/Language.h"
#include <sstream>
#include <iomanip>
#include "LootStore.h"
#include "LootItem.h"
#include "Loot.h"

INSTANTIATE_SINGLETON_1(LootMgr);

//
// --------- LootItem ---------
//



//
// ------- Loot Roll -------
//



//
// --------- Loot ---------
//

// Inserts the item into the loot (called by LootTemplate processors)
void Loot::AddItem(LootStoreItem const& item)
{
    if (m_lootItems.size() < MAX_NR_LOOT_ITEMS)                              // Normal drop
    {
        LootItem* lootItem = new LootItem(item, m_maxSlot++, uint32(m_threshold));

        if (!lootItem->isUnderThreshold)                                    // set flag for later to know that we have an over threshold item
            m_haveItemOverThreshold = true;

        m_lootItems.push_back(lootItem);
    }
}

// Insert item into the loot explicit way. (used for container item and Item::LoadFromDB)
void Loot::AddItem(uint32 itemid, uint32 count, uint32 randomSuffix, int32 randomPropertyId)
{
    if (m_lootItems.size() < MAX_NR_LOOT_ITEMS)                              // Normal drop
    {
        LootItem* lootItem = new LootItem(itemid, count, randomSuffix, randomPropertyId, m_maxSlot++);

        m_lootItems.push_back(lootItem);

        // add permission to pick this item to loot owner
        for (auto allowedGuid : m_ownerSet)
            lootItem->allowedGuid.emplace(allowedGuid);
    }
}

// Calls processor of corresponding LootTemplate (which handles everything including references)
bool Loot::FillLoot(uint32 loot_id, LootStore const& store, Player* lootOwner, bool /*personal*/, bool noEmptyError)
{
    // Must be provided
    if (!lootOwner)
        return false;

    LootTemplate const* tab = store.GetLootFor(loot_id);

    if (!tab)
    {
        if (!noEmptyError)
            sLog.outErrorDb("Table '%s' loot id #%u used but it doesn't have records.", store.GetName(), loot_id);
        return false;
    }

    m_lootItems.reserve(MAX_NR_LOOT_ITEMS);

    tab->Process(*this, lootOwner, store, store.IsRatesAllowed()); // Processing is done there, callback via Loot::AddItem()

    // fill the loot owners right here so its impossible from this point to change loot result
    Player* masterLooter = nullptr;
    if (m_lootMethod == MASTER_LOOT)
        masterLooter = ObjectAccessor::FindPlayer(m_masterOwnerGuid);
    else if (m_lootMethod == NOT_GROUP_TYPE_LOOT)
        return true;

    for (auto playerGuid : m_ownerSet)
    {
        Player* player = ObjectAccessor::FindPlayer(playerGuid);

        // assign permission for non chest items
        for (auto lootItem : m_lootItems)
        {
            if (player && (lootItem->AllowedForPlayer(player, GetLootTarget())))
            {
                if (!m_isChest)
                    lootItem->allowedGuid.emplace(player->GetObjectGuid());
            }
            else
            {
                if (playerGuid == m_currentLooterGuid)
                    lootItem->currentLooterPass = true;         // Some item may not be allowed for current looter, must set this flag to avoid item not distributed to other player
            }
        }
    }

    // check if item have to be rolled
    for (auto lootItem : m_lootItems)
    {
        // roll for over-threshold item if it's one-player loot
        if (lootItem->freeForAll || lootItem->lootItemType == LOOTITEM_TYPE_QUEST)
            lootItem->isUnderThreshold = true;
        else
        {
            switch (m_lootMethod)
            {
                case MASTER_LOOT:
                {
                    // roll item with quality greater than uncommon and under threshold and handle the case when master loot is disconnected
                    if (lootItem->itemProto->Quality > ITEM_QUALITY_UNCOMMON && (lootItem->isUnderThreshold || !masterLooter))
                        lootItem->isBlocked = true;
                    break;
                }

                case GROUP_LOOT:
                case NEED_BEFORE_GREED:
                {
                    lootItem->isBlocked = true;
                    break;
                }

                default:
                    break;
            }
        }
    }

    return true;
}

// Get loot status for a specified player
uint32 Loot::GetLootStatusFor(Player const* player) const
{
    uint32 status = 0;

    if (m_isFakeLoot && m_playersOpened.empty())
        return LOOT_STATUS_FAKE_LOOT;

    if (m_gold != 0)
        status |= LOOT_STATUS_CONTAIN_GOLD;

    for (auto lootItem : m_lootItems)
    {
        LootSlotType slotType = lootItem->GetSlotTypeForSharedLoot(player, this);
        if (slotType == MAX_LOOT_SLOT_TYPE)
            continue;

        status |= LOOT_STATUS_NOT_FULLY_LOOTED;

        if (lootItem->freeForAll)
            status |= LOOT_STATUS_CONTAIN_FFA;

        if (lootItem->isReleased)
            status |= LOOT_STATUS_CONTAIN_RELEASED_ITEMS;

        if (lootItem->isBlocked)
            status |= LOOT_STATUS_ONGOING_ROLL;
    }
    return status;
}

// Is there is any loot available for provided player
bool Loot::IsLootedFor(Player const* player) const
{
    return (GetLootStatusFor(player) == 0);
}

bool Loot::IsLootedForAll() const
{
    for (auto itr : m_ownerSet)
    {
        Player* player = ObjectAccessor::FindPlayer(itr);
        if (!player)
            continue;

        if (!IsLootedFor(player))
            return false;
    }
    return true;
}

bool Loot::CanLoot(Player const* player)
{
    ObjectGuid const& playerGuid = player->GetObjectGuid();

    // not in Guid list of possible owner mean cheat
    if (m_lootMethod != NOT_GROUP_TYPE_LOOT)
    {
        GuidSet::const_iterator itr = m_ownerSet.find(playerGuid);
        if (itr == m_ownerSet.end())
            return false;
    }

    uint32 lootStatus = GetLootStatusFor(player);

    // is already looted?
    if (!lootStatus)
        return false;

    if (lootStatus & LOOT_STATUS_ONGOING_ROLL)
        return true;

    // all player that have right too loot have right to loot dropped money
    if ((lootStatus & LOOT_STATUS_CONTAIN_GOLD) != 0 || (lootStatus & LOOT_STATUS_CONTAIN_FFA) != 0)
        return true;

    if (m_lootMethod == NOT_GROUP_TYPE_LOOT || m_lootMethod == FREE_FOR_ALL)
        return true;

    if (m_haveItemOverThreshold)
    {
        // master loot have always loot right when the loot contain over threshold item
        if (m_lootMethod == MASTER_LOOT && player->GetObjectGuid() == m_masterOwnerGuid)
            return true;

        // player can all loot on 'group loot' or 'need before greed' loot type
        if (m_lootMethod != MASTER_LOOT && m_lootMethod != ROUND_ROBIN)
            return true;
    }

    // if the player is the current looter (his turn to loot under threshold item) or the current looter released the loot then the player can loot
    if ((lootStatus & LOOT_STATUS_CONTAIN_RELEASED_ITEMS) != 0 || player->GetObjectGuid() == m_currentLooterGuid)
        return true;

    return false;
}

void Loot::NotifyItemRemoved(uint32 lootIndex)
{
    // notify all players that are looting this that the item was removed
    GuidSet::iterator i_next;
    for (GuidSet::iterator i = m_playersLooting.begin(); i != m_playersLooting.end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        Player* plr = ObjectAccessor::FindPlayer(*i);

        if (plr && plr->GetSession())
            NotifyItemRemoved(plr, lootIndex);
        else
            m_playersLooting.erase(i);
    }
}

void Loot::NotifyItemRemoved(Player* player, uint32 lootIndex) const
{
    // notify a player that are looting this that the item was removed
    WorldPacket data(SMSG_LOOT_REMOVED, 1);
    data << uint8(lootIndex);
    player->GetSession()->SendPacket(data);
}

void Loot::NotifyMoneyRemoved()
{
    // notify all players that are looting this that the money was removed
    GuidSet::iterator i_next;
    for (GuidSet::iterator i = m_playersLooting.begin(); i != m_playersLooting.end(); i = i_next)
    {
        i_next = i;
        ++i_next;
        Player* plr = ObjectAccessor::FindPlayer(*i);
        if (plr && plr->GetSession())
        {
            WorldPacket data(SMSG_LOOT_CLEAR_MONEY, 0);
            plr->GetSession()->SendPacket(data);
        }
        else
            m_playersLooting.erase(i);
    }
}

void Loot::GenerateMoneyLoot(uint32 minAmount, uint32 maxAmount)
{
    if (maxAmount > 0)
    {
        if (maxAmount <= minAmount)
            m_gold = uint32(maxAmount * sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_MONEY));
        else if ((maxAmount - minAmount) < 32700)
            m_gold = uint32(urand(minAmount, maxAmount) * sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_MONEY));
        else
            m_gold = uint32(urand(minAmount >> 8, maxAmount >> 8) * sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_MONEY)) << 8;
    }
}

void Loot::SendReleaseFor(ObjectGuid const& guid)
{
    Player* plr = sObjectAccessor.FindPlayer(guid);
    if (plr && plr->GetSession())
        SendReleaseFor(plr);
}

// no check for null pointer so it must be valid
void Loot::SendReleaseFor(Player* plr)
{
    WorldPacket data(SMSG_LOOT_RELEASE_RESPONSE, (8 + 1));
    data << m_guidTarget;
    data << uint8(1);
    plr->GetSession()->SendPacket(data);
    SetPlayerIsNotLooting(plr);
}

void Loot::SendReleaseForAll()
{
    GuidSet::iterator itr = m_playersLooting.begin();
    while (itr != m_playersLooting.end())
        SendReleaseFor(*itr++);
}

void Loot::SetPlayerIsLooting(Player* player)
{
    m_playersLooting.insert(player->GetObjectGuid());      // add 'this' player as one of the players that are looting 'loot'
    player->SetLootGuid(m_guidTarget);                     // used to keep track of what loot is opened for that player
    if (m_lootType == LOOT_CORPSE || m_isChest)
    {
        player->SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_LOOTING);
        if (m_guidTarget.IsGameObject())
            static_cast<GameObject*>(m_lootTarget)->SetInUse(true);
    }
}

void Loot::SetPlayerIsNotLooting(Player* player)
{
    m_playersLooting.erase(player->GetObjectGuid());
    player->SetLootGuid(ObjectGuid());
    if (m_lootType == LOOT_CORPSE || m_isChest)
    {
        player->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_LOOTING);
        if (m_guidTarget.IsGameObject())
            static_cast<GameObject*>(m_lootTarget)->SetInUse(false);
    }
}

void Loot::Release(Player* player)
{
    bool updateClients = false;
    if (player->GetObjectGuid() == m_currentLooterGuid || player->GetObjectGuid() == m_masterOwnerGuid)
    {
        // the owner of the loot released his item
        for (auto lootItem : m_lootItems)
        {
            auto slotType = lootItem->GetSlotTypeForSharedLoot(player, this);

            if (slotType == LOOT_SLOT_VIEW || slotType == MAX_LOOT_SLOT_TYPE)
                continue;

            // do not release blocked item (rolling ongoing)
            if (!lootItem->isBlocked && !lootItem->isReleased)
            {
                lootItem->isReleased = true;
                updateClients = true;
            }
        }
    }

    switch (m_guidTarget.GetHigh())
    {
        case HIGHGUID_GAMEOBJECT:
        {
            GameObject* go = (GameObject*) m_lootTarget;
            SetPlayerIsNotLooting(player);

            // GO is mineral vein? so it is not removed after its looted
            switch (go->GetGoType())
            {
                case GAMEOBJECT_TYPE_DOOR:
                    // locked doors are opened with spelleffect openlock, prevent remove its as looted
                    go->UseDoorOrButton();
                    break;
                case GAMEOBJECT_TYPE_CHEST:
                {
                    if (!IsLootedForAll())
                    {
                        updateClients = true;
                        break;
                    }

                    uint32 go_min = go->GetGOInfo()->chest.minSuccessOpens;
                    uint32 go_max = go->GetGOInfo()->chest.maxSuccessOpens;
                    bool refill = false;

                    // only vein pass this check
                    if (go_min != 0 && go_max > go_min)
                    {
                        float amount_rate = sWorld.getConfig(CONFIG_FLOAT_RATE_MINING_AMOUNT);
                        float min_amount = go_min * amount_rate;
                        float max_amount = go_max * amount_rate;

                        go->AddUse();
                        float uses = float(go->GetUseCount());
                        if (uses < max_amount)
                        {
                            if (uses >= min_amount)
                            {
                                float chance_rate = sWorld.getConfig(CONFIG_FLOAT_RATE_MINING_NEXT);

                                int32 ReqValue = 175;
                                LockEntry const* lockInfo = sLockStore.LookupEntry(go->GetGOInfo()->chest.lockId);
                                if (lockInfo)
                                    ReqValue = lockInfo->Skill[0];
                                float skill = float(player->GetSkillValue(SKILL_MINING)) / (ReqValue + 25);
                                double chance = pow(0.8 * chance_rate, 4 * (1 / double(max_amount)) * double(uses));
                                if (roll_chance_f(float(100.0f * chance + skill)))
                                    refill = true;
                            }
                            else
                                refill = true;  // 100% chance until min uses
                        }
                    }

                    if (refill)
                    {
                        // this vein have can be now refilled, as this is a vein no other player are looting it so no need to send them release
                        Clear();                // clear the content and reset some values
                        FillLoot(go->GetGOInfo()->GetLootId(), LootTemplates_Gameobject, player, false); // refill the loot with new items
                        go->SetLootState(GO_READY);
                    }
                    else
                        go->SetLootState(GO_JUST_DEACTIVATED);

                    break;
                }
                case GAMEOBJECT_TYPE_FISHINGHOLE:
                    // The fishing hole used once more
                    go->AddUse();                           // if the max usage is reached, will be despawned at next tick
                    if (go->GetUseCount() >= urand(go->GetGOInfo()->fishinghole.minSuccessOpens, go->GetGOInfo()->fishinghole.maxSuccessOpens))
                    {
                        go->SetLootState(GO_JUST_DEACTIVATED);
                    }
                    else
                        go->SetLootState(GO_READY);
                    break;
                default:
                    go->SetLootState(GO_JUST_DEACTIVATED);
                    break;
            }
            break;
        }
        case HIGHGUID_CORPSE:                               // ONLY remove insignia at BG
        {
            Corpse* corpse = (Corpse*) m_lootTarget;
            if (!corpse || !corpse->IsWithinDistInMap(player, INTERACTION_DISTANCE))
                return;

            if (IsLootedFor(player))
            {
                Clear();
                corpse->RemoveFlag(CORPSE_FIELD_DYNAMIC_FLAGS, CORPSE_DYNFLAG_LOOTABLE);
            }
            break;
        }
        case HIGHGUID_ITEM:
        {
            ForceLootAnimationClientUpdate();
            switch (m_lootType)
            {
                // temporary loot in stacking items, clear loot state, no auto loot move
                case LOOT_PROSPECTING:
                {
                    uint32 count = m_itemTarget->GetCount();

                    // >=5 checked in spell code, but will work for cheating cases also with removing from another stacks.
                    if (count > 5)
                        count = 5;

                    // reset loot for allow repeat looting if stack > 5
                    Clear();
                    m_itemTarget->SetLootState(ITEM_LOOT_REMOVED);
                    player->DestroyItemCount(m_itemTarget, count, true);
                    break;
                }
                // temporary loot, auto loot move
                case LOOT_DISENCHANTING:
                {
                    if (!IsLootedFor(player))
                        AutoStore(player); // can be lost if no space
                    Clear();
                    m_itemTarget->SetLootState(ITEM_LOOT_REMOVED);
                    player->DestroyItem(m_itemTarget->GetBagSlot(), m_itemTarget->GetSlot(), true);
                    break;
                }
                // normal persistence loot
                default:
                {
                    // must be destroyed only if no loot
                    if (IsLootedFor(player))
                    {
                        m_itemTarget->SetLootState(ITEM_LOOT_REMOVED);
                        player->DestroyItem(m_itemTarget->GetBagSlot(), m_itemTarget->GetSlot(), true);
                    }
                    break;
                }
            }
            //already done above
            updateClients = false;
            break;
        }
        case HIGHGUID_UNIT:
        {
            switch (m_lootType)
            {
                case LOOT_PICKPOCKETING:
                {
                    if (IsLootedFor(player))
                    {
                        Creature* creature = (Creature*)m_lootTarget;
                        creature->SetLootStatus(CREATURE_LOOT_STATUS_PICKPOCKETED);
                    }
                    break;
                }
                case LOOT_SKINNING:
                {
                    SetPlayerIsNotLooting(player);
                    Creature* creature = (Creature*)m_lootTarget;
                    if (IsLootedFor(player))
                    {
                        creature->SetLootStatus(CREATURE_LOOT_STATUS_SKINNED);
                    }
                    else
                    {
                        // player released his skin so we can make it available for everyone
                        if (creature->GetLootStatus() != CREATURE_LOOT_STATUS_SKIN_AVAILABLE)
                        {
                            creature->SetLootStatus(CREATURE_LOOT_STATUS_SKIN_AVAILABLE);
                            updateClients = true;
                        }
                    }
                    break;
                }
                case LOOT_CORPSE:
                {
                    Creature* creature = (Creature*)m_lootTarget;
                    SetPlayerIsNotLooting(player);
                    updateClients = true;

                    if (m_isFakeLoot)
                    {
                        SendReleaseForAll();
                        creature->SetLootStatus(CREATURE_LOOT_STATUS_LOOTED);
                        break;
                    }

                    if (IsLootedForAll())
                    {
                        SendReleaseForAll();
                        creature->SetLootStatus(CREATURE_LOOT_STATUS_LOOTED);
                    }
                    break;
                }
                default:
                    break;
            }
            break;
        }
        default:
            break;
    }

    if (updateClients)
        ForceLootAnimationClientUpdate();
}

// Popup windows with loot content
void Loot::ShowContentTo(Player* plr)
{
    if (!m_isChest)
    {
        // for item loot that might be empty we should not display error but instead send empty loot window
        if (!m_lootItems.empty() && !CanLoot(plr))
        {
            SendReleaseFor(plr);
            sLog.outError("Loot::ShowContentTo()> %s is trying to open a loot without credential", plr->GetGuidStr().c_str());
            return;
        }

        // add this player to the the openers list of this loot
        m_playersOpened.emplace(plr->GetObjectGuid());
    }
    else
    {
        if (static_cast<GameObject*>(m_lootTarget)->IsInUse())
        {
            plr->SendLootError(m_guidTarget, LOOT_ERROR_LOCKED);
            return;
        }

        if (m_ownerSet.find(plr->GetObjectGuid()) == m_ownerSet.end())
            SetGroupLootRight(plr);
    }

    if (m_lootMethod != NOT_GROUP_TYPE_LOOT && !m_isChecked)
        GroupCheck();

    WorldPacket data(SMSG_LOOT_RESPONSE);
    data << m_guidTarget;
    data << uint8(m_clientLootType);

    GetLootContentFor(plr, data);                           // fill the data with items contained in the loot (may be empty)
    SetPlayerIsLooting(plr);
    if (m_lootTarget)
        m_lootTarget->InspectingLoot();

    plr->SendDirectMessage(data);
}

void Loot::GroupCheck()
{
    m_isChecked = true;
    PlayerList playerList;
    Player* masterLooter = nullptr;
    for (auto playerGuid : m_ownerSet)
    {
        Player* player = sObjectAccessor.FindPlayer(playerGuid);
        if (!player)
            continue;

        if (!player->GetSession())
            continue;

        playerList.emplace_back(player);

        if (m_lootMethod == MASTER_LOOT)
        {
            if (!masterLooter && playerGuid == m_masterOwnerGuid)
                masterLooter = player;
        }

        // check if there is need to launch a roll
        for (auto lootItem : m_lootItems)
        {
            if (!lootItem->isBlocked)
                continue;

            uint32 itemSlot = lootItem->lootSlot;

            /*if (m_roll.find(itemSlot) == m_roll.end() && lootItem->IsAllowed(player, this))
            {
                if (!m_roll[itemSlot].TryToStart(*this, itemSlot))      // Create and try to start a roll
                    m_roll.erase(m_roll.find(itemSlot));                // Cannot start roll so we have to delete it (find will not fail as the item was just created)
            }*/
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

bool IsEligibleForLoot(Player* looter, WorldObject* lootTarget)
{
    if (looter->IsAtGroupRewardDistance(lootTarget))
        return true;

    if (lootTarget->GetTypeId() == TYPEID_UNIT)
    {
        Unit* creature = (Unit*)lootTarget;
        return creature->getThreatManager().HasThreat(looter);
    }

    return false;
}

// Set the player who have right for this loot
void Loot::SetGroupLootRight(Player* player)
{
    if (m_isChest && !m_ownerSet.empty())
    {
        // chest was already opened so we dont have to change group type
        m_ownerSet.emplace(player->GetObjectGuid());
        return;
    }

    m_ownerSet.clear();
    Group* grp = player->GetGroup();
    if (grp && (!m_isChest || static_cast<GameObject*>(m_lootTarget)->GetGOInfo()->chest.groupLootRules))
    {
        m_lootMethod = grp->GetLootMethod();
        m_threshold = grp->GetLootThreshold();

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

            if (IsEligibleForLoot(looter, m_lootTarget))
            {
                m_ownerSet.insert(itr->guid);   // save this guid to main owner set
                ownerList.push_back(itr->guid); // save this guid to local ordered GuidList (we need to keep it ordered only here)

                // get enchant skill of authorized looter
                uint32 enchantSkill = looter->GetSkillValue(SKILL_ENCHANTING);
                if (m_maxEnchantSkill < enchantSkill)
                    m_maxEnchantSkill = enchantSkill;
            }

        }
        while (itr != currentLooterItr);

        if (m_lootMethod == MASTER_LOOT)
        {
            m_masterOwnerGuid = grp->GetMasterLooterGuid();
            // Set group method to GROUP_LOOT if no master loot found
            if (m_ownerSet.find(m_masterOwnerGuid) == m_ownerSet.end())
                m_lootMethod = GROUP_LOOT;
        }

        // if more than one player have right to loot than we have to handle group method, round robin, roll, etc..
        if (m_ownerSet.size() > 1 && m_lootMethod != FREE_FOR_ALL)
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
    m_lootMethod = NOT_GROUP_TYPE_LOOT;
}

Loot::Loot(Player* player, Creature* creature, LootType type) :
    m_lootTarget(nullptr), m_itemTarget(nullptr), m_gold(0), m_maxSlot(0), m_lootType(type),
    m_clientLootType(CLIENT_LOOT_CORPSE), m_lootMethod(NOT_GROUP_TYPE_LOOT), m_threshold(ITEM_QUALITY_UNCOMMON), m_maxEnchantSkill(0), m_haveItemOverThreshold(false),
    m_isChecked(false), m_isChest(false), m_isChanged(false), m_isFakeLoot(false), m_createTime(World::GetCurrentClockTime())
{
    // the player whose group may loot the corpse
    if (!player)
    {
        sLog.outError("LootMgr::CreateLoot> Error cannot get looter info to create loot!");
        return;
    }

    if (!creature)
    {
        sLog.outError("Loot::CreateLoot> cannot create loot, no creature passed!");
        return;
    }

    m_lootTarget = creature;
    m_guidTarget = creature->GetObjectGuid();
    CreatureInfo const* creatureInfo = creature->GetCreatureInfo();

    switch (type)
    {
        case LOOT_CORPSE:
        {
            // setting loot right
            SetGroupLootRight(player);
            m_clientLootType = CLIENT_LOOT_CORPSE;

            if ((creatureInfo->LootId && FillLoot(creatureInfo->LootId, LootTemplates_Creature, player, false)) || creatureInfo->MaxLootGold > 0)
            {
                GenerateMoneyLoot(creatureInfo->MinLootGold, creatureInfo->MaxLootGold);
                // loot may be anyway empty (loot may be empty or contain items that no one have right to loot)
                bool isLootedForAll = IsLootedForAll();
                if (isLootedForAll)
                {
                    // show sometimes an empty window
                    if (sWorld.getConfig(CONFIG_BOOL_CORPSE_EMPTY_LOOT_SHOW) && urand(0, 2) == 1)
                    {
                        m_isFakeLoot = true;
                        isLootedForAll = false;
                    }
                }

                if (!isLootedForAll)
                    creature->SetFlag(UNIT_DYNAMIC_FLAGS, UNIT_DYNFLAG_LOOTABLE);
                else
                    creature->SetLootStatus(CREATURE_LOOT_STATUS_LOOTED);
                ForceLootAnimationClientUpdate();
                break;
            }

            sLog.outDebug("Loot::CreateLoot> cannot create corpse loot, FillLoot failed with loot id(%u)!", creatureInfo->LootId);
            creature->SetLootStatus(CREATURE_LOOT_STATUS_LOOTED);
            break;
        }
        case LOOT_PICKPOCKETING:
        {
            m_clientLootType = CLIENT_LOOT_PICKPOCKETING;

            // setting loot right
            m_ownerSet.insert(player->GetObjectGuid());
            m_lootMethod = NOT_GROUP_TYPE_LOOT;

            if (!creatureInfo->PickpocketLootId || !FillLoot(creatureInfo->PickpocketLootId, LootTemplates_Pickpocketing, player, false))
            {
                sLog.outError("Loot::CreateLoot> cannot create pickpocket loot, FillLoot failed with loot id(%u)!", creatureInfo->PickpocketLootId);
                return;
            }

            // Generate extra money for pick pocket loot
            const uint32 a = urand(0, creature->getLevel() / 2);
            const uint32 b = urand(0, player->getLevel() / 2);
            m_gold = uint32(10 * (a + b) * sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_MONEY));

            break;
        }
        case LOOT_SKINNING:
        {
            // setting loot right
            m_ownerSet.insert(player->GetObjectGuid());
            m_clientLootType = CLIENT_LOOT_PICKPOCKETING;
            m_lootMethod = NOT_GROUP_TYPE_LOOT;
            if (!creatureInfo->SkinningLootId || !FillLoot(creatureInfo->SkinningLootId, LootTemplates_Skinning, player, false))
            {
                sLog.outError("Loot::CreateLoot> cannot create skinning loot, FillLoot failed with loot id(%u)!", creatureInfo->SkinningLootId);
                return;
            }
            break;
        }
        default:
            sLog.outError("Loot::CreateLoot> Cannot create loot for %s with invalid LootType(%u)", creature->GetGuidStr().c_str(), uint32(type));
            break;
    }

    return;
}

Loot::Loot(Player* player, GameObject* gameObject, LootType type) :
    m_lootTarget(nullptr), m_itemTarget(nullptr), m_gold(0), m_maxSlot(0), m_lootType(type),
    m_clientLootType(CLIENT_LOOT_CORPSE), m_lootMethod(NOT_GROUP_TYPE_LOOT), m_threshold(ITEM_QUALITY_UNCOMMON), m_maxEnchantSkill(0), m_haveItemOverThreshold(false),
    m_isChecked(false), m_isChest(false), m_isChanged(false), m_isFakeLoot(false), m_createTime(World::GetCurrentClockTime())
{
    // the player whose group may loot the corpse
    if (!player)
    {
        sLog.outError("LootMgr::CreateLoot> Error cannot get looter info to create loot!");
        return;
    }

    if (!gameObject)
    {
        sLog.outError("Loot::CreateLoot> cannot create game object loot, no game object passed!");
        return;
    }

    m_lootTarget = gameObject;
    m_guidTarget = gameObject->GetObjectGuid();

    // not check distance for GO in case owned GO (fishing bobber case, for example)
    // And permit out of range GO with no owner in case fishing hole
    if ((type != LOOT_FISHINGHOLE &&
            ((type != LOOT_FISHING && type != LOOT_FISHING_FAIL) || gameObject->GetOwnerGuid() != player->GetObjectGuid()) &&
            !gameObject->IsWithinDistInMap(player, INTERACTION_DISTANCE)))
    {
        sLog.outError("Loot::CreateLoot> cannot create game object loot, basic check failed for gameobject %u!", gameObject->GetEntry());
        return;
    }

    // generate loot only if ready for open and spawned in world
    if (gameObject->GetLootState() == GO_READY && gameObject->IsSpawned())
    {
        if ((gameObject->GetEntry() == BG_AV_OBJECTID_MINE_N || gameObject->GetEntry() == BG_AV_OBJECTID_MINE_S))
        {
            if (BattleGround* bg = player->GetBattleGround())
                if (bg->GetTypeID() == BATTLEGROUND_AV)
                    if (!(((BattleGroundAV*)bg)->PlayerCanDoMineQuest(gameObject->GetEntry(), player->GetTeam())))
                    {
                        return;
                    }
        }

        switch (type)
        {
            case LOOT_FISHING_FAIL:
            {
                // setting loot right
                m_ownerSet.insert(player->GetObjectGuid());
                m_lootMethod = NOT_GROUP_TYPE_LOOT;
                m_clientLootType = CLIENT_LOOT_FISHING;

                // Entry 0 in fishing loot template used for store junk fish loot at fishing fail it junk allowed by config option
                // this is overwrite fishinghole loot for example
                FillLoot(0, LootTemplates_Fishing, player, true);
                break;
            }
            case LOOT_FISHING:
            {
                // setting loot right
                m_ownerSet.insert(player->GetObjectGuid());
                m_lootMethod = NOT_GROUP_TYPE_LOOT;
                m_clientLootType = CLIENT_LOOT_FISHING;

                uint32 zone, subzone;
                gameObject->GetZoneAndAreaId(zone, subzone);
                // if subzone loot exist use it
                if (!FillLoot(subzone, LootTemplates_Fishing, player, true, (subzone != zone)) && subzone != zone)
                    // else use zone loot (if zone diff. from subzone, must exist in like case)
                    FillLoot(zone, LootTemplates_Fishing, player, true);
                break;
            }
            default:
            {
                if (uint32 lootid = gameObject->GetGOInfo()->GetLootId())
                {
                    if (gameObject->GetGOInfo()->type == GAMEOBJECT_TYPE_CHEST)
                        m_isChest = true;

                    SetGroupLootRight(player);
                    FillLoot(lootid, LootTemplates_Gameobject, player, false);
                    GenerateMoneyLoot(gameObject->GetGOInfo()->MinMoneyLoot, gameObject->GetGOInfo()->MaxMoneyLoot);

                    if (m_lootType == LOOT_FISHINGHOLE)
                        m_clientLootType = CLIENT_LOOT_FISHING;
                    else
                        m_clientLootType = CLIENT_LOOT_PICKPOCKETING;
                }
                break;
            }
        }

        gameObject->SetLootState(GO_ACTIVATED);
    }
    return;
}

Loot::Loot(Player* player, Corpse* corpse, LootType type) :
    m_lootTarget(nullptr), m_itemTarget(nullptr), m_gold(0), m_maxSlot(0), m_lootType(type),
    m_clientLootType(CLIENT_LOOT_CORPSE), m_lootMethod(NOT_GROUP_TYPE_LOOT), m_threshold(ITEM_QUALITY_UNCOMMON), m_maxEnchantSkill(0), m_haveItemOverThreshold(false),
    m_isChecked(false), m_isChest(false), m_isChanged(false), m_isFakeLoot(false), m_createTime(World::GetCurrentClockTime())
{
    // the player whose group may loot the corpse
    if (!player)
    {
        sLog.outError("LootMgr::CreateLoot> Error cannot get looter info to create loot!");
        return;
    }

    if (!corpse)
    {
        sLog.outError("Loot::CreateLoot> cannot create corpse loot, no corpse passed!");
        return;
    }

    m_lootTarget = corpse;
    m_guidTarget = corpse->GetObjectGuid();

    if (type != LOOT_INSIGNIA || corpse->GetType() == CORPSE_BONES)
        return;

    if (!corpse->lootForBody)
    {
        corpse->lootForBody = true;
        uint32 pLevel;
        if (Player* plr = sObjectAccessor.FindPlayer(corpse->GetOwnerGuid()))
            pLevel = plr->getLevel();
        else
            pLevel = player->getLevel(); // TODO:: not correct, need to save real player level in the corpse data in case of logout

         m_ownerSet.insert(player->GetObjectGuid());
         m_lootMethod = NOT_GROUP_TYPE_LOOT;
         m_clientLootType = CLIENT_LOOT_CORPSE;

        if (player->GetBattleGround()->GetTypeID() == BATTLEGROUND_AV)
            FillLoot(0, LootTemplates_Creature, player, false);

        // It may need a better formula
        // Now it works like this: lvl10: ~6copper, lvl70: ~9silver
        m_gold = (uint32)(urand(50, 150) * 0.016f * pow(((float)pLevel) / 5.76f, 2.5f) * sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_MONEY));
    }
    return;
}

Loot::Loot(Player* player, Item* item, LootType type) :
    m_lootTarget(nullptr), m_itemTarget(nullptr), m_gold(0), m_maxSlot(0), m_lootType(type),
    m_clientLootType(CLIENT_LOOT_CORPSE), m_lootMethod(NOT_GROUP_TYPE_LOOT), m_threshold(ITEM_QUALITY_UNCOMMON), m_maxEnchantSkill(0), m_haveItemOverThreshold(false),
    m_isChecked(false), m_isChest(false), m_isChanged(false), m_isFakeLoot(false), m_createTime(World::GetCurrentClockTime())
{
    // the player whose group may loot the corpse
    if (!player)
    {
        sLog.outError("LootMgr::CreateLoot> Error cannot get looter info to create loot!");
        return;
    }

    if (!item)
    {
        sLog.outError("Loot::CreateLoot> cannot create item loot, no item passed!");
        return;
    }

    m_itemTarget = item;
    m_guidTarget = item->GetObjectGuid();

    m_ownerSet.insert(player->GetObjectGuid());
    m_lootMethod = NOT_GROUP_TYPE_LOOT;
    m_clientLootType = CLIENT_LOOT_PICKPOCKETING;
    switch (type)
    {
        case LOOT_DISENCHANTING:
            FillLoot(item->GetProto()->DisenchantID, LootTemplates_Disenchant, player, true);
            item->SetLootState(ITEM_LOOT_TEMPORARY);
            break;
        case LOOT_PROSPECTING:
            FillLoot(item->GetEntry(), LootTemplates_Prospecting, player, true);
            item->SetLootState(ITEM_LOOT_TEMPORARY);
            break;
        default:
            FillLoot(item->GetEntry(), LootTemplates_Item, player, true, item->GetProto()->MaxMoneyLoot == 0);
            GenerateMoneyLoot(item->GetProto()->MinMoneyLoot, item->GetProto()->MaxMoneyLoot);
            item->SetLootState(ITEM_LOOT_CHANGED);
            break;
    }
    return;
}

Loot::Loot(Unit* unit, Item* item) :
    m_lootTarget(nullptr), m_itemTarget(item), m_gold(0), m_maxSlot(0),
    m_lootType(LOOT_SKINNING), m_clientLootType(CLIENT_LOOT_PICKPOCKETING), m_lootMethod(NOT_GROUP_TYPE_LOOT), m_threshold(ITEM_QUALITY_UNCOMMON), m_maxEnchantSkill(0),
    m_haveItemOverThreshold(false), m_isChecked(false), m_isChest(false), m_isChanged(false), m_isFakeLoot(false), m_createTime(World::GetCurrentClockTime())
{
    m_ownerSet.insert(unit->GetObjectGuid());
    m_guidTarget = item->GetObjectGuid();
}

Loot::Loot(Player* player, uint32 id, LootType type) :
    m_lootTarget(nullptr), m_itemTarget(nullptr), m_gold(0), m_maxSlot(0), m_lootType(type),
    m_clientLootType(CLIENT_LOOT_CORPSE), m_lootMethod(NOT_GROUP_TYPE_LOOT), m_threshold(ITEM_QUALITY_UNCOMMON), m_maxEnchantSkill(0), m_haveItemOverThreshold(false),
    m_isChecked(false), m_isChest(false), m_isChanged(false), m_isFakeLoot(false), m_createTime(World::GetCurrentClockTime())
{
    m_ownerSet.insert(player->GetObjectGuid());
    switch (type)
    {
        case LOOT_MAIL:
            FillLoot(id, LootTemplates_Mail, player, true, true);
            m_clientLootType = CLIENT_LOOT_PICKPOCKETING;
            break;
        case LOOT_SKINNING:
            FillLoot(id, LootTemplates_Skinning, player, true, true);
            m_clientLootType = CLIENT_LOOT_PICKPOCKETING;
            break;
        default:
            sLog.outError("Loot::Loot> invalid loot type passed to loot constructor.");
            break;
    }
}

Loot::Loot(LootType type) :
    m_lootTarget(nullptr), m_itemTarget(nullptr), m_gold(0), m_maxSlot(0), m_lootType(type),
    m_clientLootType(CLIENT_LOOT_CORPSE), m_lootMethod(NOT_GROUP_TYPE_LOOT), m_threshold(ITEM_QUALITY_UNCOMMON), m_maxEnchantSkill(0), m_haveItemOverThreshold(false),
    m_isChecked(false), m_isChest(false), m_isChanged(false), m_isFakeLoot(false), m_createTime(World::GetCurrentClockTime())
{

}

void Loot::SendAllowedLooter()
{
    if (m_lootMethod == FREE_FOR_ALL || m_lootMethod == NOT_GROUP_TYPE_LOOT)
        return;

    WorldPacket data(SMSG_LOOT_LIST);
    data << GetLootGuid();

    if (m_lootMethod == MASTER_LOOT)
        data << m_masterOwnerGuid.WriteAsPacked();
    else
        data << uint8(0);

    data << m_currentLooterGuid.WriteAsPacked();

    for (auto itr : m_ownerSet)
        if (Player* plr = ObjectAccessor::FindPlayer(itr))
            plr->GetSession()->SendPacket(data);
}

InventoryResult Loot::SendItem(Player* target, uint32 itemSlot)
{
    LootItem* lootItem = GetLootItemInSlot(itemSlot);
    return SendItem(target, lootItem);
}

InventoryResult Loot::SendItem(Player* target, LootItem* lootItem)
{
    if (!target)
        return EQUIP_ERR_OUT_OF_RANGE;

    if (!lootItem)
    {
        if (target->GetSession())
            SendReleaseFor(target);
        return EQUIP_ERR_ITEM_NOT_FOUND;
    }

    bool playerGotItem = false;
    InventoryResult msg = EQUIP_ERR_CANT_DO_RIGHT_NOW;
    if (target->GetSession())
    {
        ItemPosCountVec dest;
        msg = target->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, lootItem->itemId, lootItem->count);
        if (msg == EQUIP_ERR_OK)
        {
            Item* newItem = target->StoreNewItem(dest, lootItem->itemId, true, lootItem->randomPropertyId);

            if (lootItem->freeForAll)
            {
                NotifyItemRemoved(target, lootItem->lootSlot);
                sLog.outDebug("This item is free for all!!");
            }
            else
                NotifyItemRemoved(lootItem->lootSlot);

            target->SendNewItem(newItem, uint32(lootItem->count), false, false, true);

            if (!m_isChest)
            {
                // for normal loot the players right was set at loot filling so we just have to remove from allowed guids
                if (lootItem->freeForAll)
                    lootItem->allowedGuid.erase(target->GetObjectGuid());
                else
                    lootItem->allowedGuid.clear();
            }
            else
            {
                // for chest as the allowed guid should be empty we will add the looter guid so that mean it was looted from target
                lootItem->allowedGuid.emplace(target->GetObjectGuid());
            }

            playerGotItem = true;
            m_isChanged = true;
        }
        else
            target->SendEquipError(msg, nullptr, nullptr, lootItem->itemId);
    }

    if (!playerGotItem)
    {
        // an error occurred player didn't received his loot
        lootItem->isBlocked = false;                                  // make the item available (was blocked since roll started)
        m_currentLooterGuid = target->GetObjectGuid();                // change looter guid to let only him right to loot
        lootItem->isReleased = false;                                 // be sure the loot was not already released by another player
        SendAllowedLooter();                                          // update the looter right for client
    }
    else
    {
        if (IsLootedForAll())
        {
            SendReleaseForAll();
            if (m_isChest)
            {
                GameObject* go = (GameObject*)m_lootTarget;
                uint32 go_min = go->GetGOInfo()->chest.minSuccessOpens;
                uint32 go_max = go->GetGOInfo()->chest.maxSuccessOpens;
                // only vein pass this check
                if (go_min != 0 && go_max > go_min)
                {
                    // nothing to do refill is handled in Loot::Release()
                }
                else
                    go->SetLootState(GO_JUST_DEACTIVATED);
            }
        }
        else if (IsLootedFor(target))
            SendReleaseFor(target);
        ForceLootAnimationClientUpdate();
    }
    return msg;
}

bool Loot::AutoStore(Player* player, bool broadcast /*= false*/, uint32 bag /*= NULL_BAG*/, uint32 slot /*= NULL_SLOT*/)
{
    bool result = true;
    for (LootItemList::const_iterator lootItemItr = m_lootItems.begin(); lootItemItr != m_lootItems.end(); ++lootItemItr)
    {
        LootItem* lootItem = *lootItemItr;

        if (!lootItem->IsAllowed(player, this))
            continue; // already looted or not allowed

        ItemPosCountVec dest;
        InventoryResult msg = player->CanStoreNewItem(bag, slot, dest, lootItem->itemId, lootItem->count);
        if (msg != EQUIP_ERR_OK && slot != NULL_SLOT)
            msg = player->CanStoreNewItem(bag, NULL_SLOT, dest, lootItem->itemId, lootItem->count);
        if (msg != EQUIP_ERR_OK && bag != NULL_BAG)
            msg = player->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, lootItem->itemId, lootItem->count);
        if (msg != EQUIP_ERR_OK)
        {
            player->SendEquipError(msg, nullptr, nullptr, lootItem->itemId);
            result = false;
            continue;
        }

        if (lootItem->freeForAll)
            lootItem->allowedGuid.erase(player->GetObjectGuid());
        else
            lootItem->allowedGuid.clear();

        Item* pItem = player->StoreNewItem(dest, lootItem->itemId, true, lootItem->randomPropertyId);
        player->SendNewItem(pItem, lootItem->count, false, false, broadcast);
        m_isChanged = true;
    }

    return result;
}

void Loot::Update()
{
    m_isChanged = false;

}

// this will force server to update all client that is showing this object
// used to update players right to loot or sparkles animation
void Loot::ForceLootAnimationClientUpdate() const
{
    if (!m_lootTarget)
        return;

    switch (m_lootTarget->GetTypeId())
    {
        case TYPEID_UNIT:
            m_lootTarget->ForceValuesUpdateAtIndex(UNIT_DYNAMIC_FLAGS);
            break;
        case TYPEID_GAMEOBJECT:
            return;
            // we have to update sparkles/loot for this object
            if (m_isChest)
                m_lootTarget->ForceValuesUpdateAtIndex(GAMEOBJECT_DYN_FLAGS);
            break;
        default:
            break;
    }
}

// will return the pointer of item in loot slot provided without any right check
LootItem* Loot::GetLootItemInSlot(uint32 itemSlot)
{
    for (auto lootItem : m_lootItems)
    {
        if (lootItem->lootSlot == itemSlot)
            return lootItem;
    }
    return nullptr;
}

// Will return available loot item for specific player. Use only for own loot like loot in item and mail
void Loot::GetLootItemsListFor(Player* player, LootItemList& lootList)
{
    for (LootItemList::const_iterator lootItemItr = m_lootItems.begin(); lootItemItr != m_lootItems.end(); ++lootItemItr)
    {
        LootItem* lootItem = *lootItemItr;
        if (!lootItem->IsAllowed(player, this))
            continue; // already looted or player have no right to see/loot this item

        lootList.push_back(lootItem);
    }
}

Loot::~Loot()
{
    SendReleaseForAll();
    for (auto& m_lootItem : m_lootItems)
        delete m_lootItem;
}

void Loot::Clear()
{
    for (auto& m_lootItem : m_lootItems)
        delete m_lootItem;
    m_lootItems.clear();
    m_playersLooting.clear();
    m_gold = 0;
    m_ownerSet.clear();
    m_masterOwnerGuid.Clear();
    m_currentLooterGuid.Clear();
    //m_roll.clear();
    m_maxEnchantSkill = 0;
    m_haveItemOverThreshold = false;
    m_isChecked = false;
    m_maxSlot = 0;
}

// only used from explicitly loaded loot
void Loot::SetGoldAmount(uint32 _gold)
{
    if (m_lootType == LOOT_SKINNING)
        m_gold = _gold;
}

void Loot::SendGold(Player* player)
{
    NotifyMoneyRemoved();

    if (m_lootMethod != NOT_GROUP_TYPE_LOOT)           // item can be looted only single player
    {
        uint32 money_per_player = uint32(m_gold / (m_ownerSet.size()));

        for (auto itr : m_ownerSet)
        {
            Player* plr = sObjectMgr.GetPlayer(itr);
            if (!plr || !plr->GetSession())
                continue;

            plr->ModifyMoney(money_per_player);

            WorldPacket data(SMSG_LOOT_MONEY_NOTIFY, 4);
            data << uint32(money_per_player);

            plr->GetSession()->SendPacket(data);
        }
    }
    else
    {
        player->ModifyMoney(m_gold);

        if (m_guidTarget.IsItem())
        {
            if (Item* item = player->GetItemByGuid(m_guidTarget))
                item->SetLootState(ITEM_LOOT_CHANGED);
        }
    }
    m_gold = 0;

    // animation update is done in Release if needed.
    if (IsLootedFor(player))
    {
        Release(player);
        // Be aware that in case of items that contain loot this class may be freed.
        // All pointers may be invalid due to Player::DestroyItem call.
    }
    else
        ForceLootAnimationClientUpdate();
}

bool Loot::IsItemAlreadyIn(uint32 itemId) const
{
    for (auto lootItem : m_lootItems)
    {
        if (lootItem->itemId == itemId)
            return true;
    }
    return false;
}

void Loot::PrintLootList(ChatHandler& chat, WorldSession* session) const
{
    if (!session)
    {
        chat.SendSysMessage("Error you have to be in game for this command.");
        return;
    }

    if (m_gold == 0)
        chat.PSendSysMessage("Loot have no money");
    else
        chat.PSendSysMessage("Loot have (%u)coppers", m_gold);

    if (m_lootItems.empty())
    {
        chat.PSendSysMessage("Loot have no item.");
        return;
    }

    for (auto lootItem : m_lootItems)
    {
        uint32 itemId = lootItem->itemId;
        ItemPrototype const* pProto = sItemStorage.LookupEntry<ItemPrototype >(itemId);
        if (!pProto)
            continue;

        int loc_idx = session->GetSessionDbLocaleIndex();

        std::string name = pProto->Name1;
        sObjectMgr.GetItemLocaleStrings(itemId, loc_idx, &name);
        std::string count = "x" + std::to_string(lootItem->count);
        chat.PSendSysMessage(LANG_ITEM_LIST_CHAT, itemId, itemId, name.c_str(), count.c_str());
    }
}

// fill in the bytebuffer with loot content for specified player
void Loot::GetLootContentFor(Player* player, ByteBuffer& buffer)
{
    uint8 itemsShown = 0;

    // gold
    buffer << uint32(m_gold);

    size_t count_pos = buffer.wpos();                            // pos of item count byte
    buffer << uint8(0);                                          // item count placeholder

    for (LootItemList::const_iterator lootItemItr = m_lootItems.begin(); lootItemItr != m_lootItems.end(); ++lootItemItr)
    {
        LootItem* lootItem = *lootItemItr;
        LootSlotType slot_type = lootItem->GetSlotTypeForSharedLoot(player, this);
        if (slot_type >= MAX_LOOT_SLOT_TYPE)
        {
            sLog.outDebug("Item not visible for %s> itemid(%u) in slot (%u)!", player->GetGuidStr().c_str(), lootItem->itemId, uint32(lootItem->lootSlot));
            continue;
        }

        buffer << uint8(lootItem->lootSlot);
        buffer << *lootItem;
        buffer << uint8(slot_type);                              // 0 - get 1 - look only 2 - master selection
        ++itemsShown;

        sLog.outDebug("Sending loot to %s> itemid(%u) in slot (%u)!", player->GetGuidStr().c_str(), lootItem->itemId, uint32(lootItem->lootSlot));
    }

    // update number of items shown
    buffer.put<uint8>(count_pos, itemsShown);
}

ByteBuffer& operator<<(ByteBuffer& b, LootItem const& li)
{
    b << uint8(li.lootSlot);
    b << uint32(li.itemId);
    b << uint32(li.count);                                  // nr of items of this type
    b << uint32(ObjectMgr::GetItemPrototype(li.itemId)->DisplayInfoID);
    b << uint32(li.randomSuffix);
    b << uint32(li.randomPropertyId);
    return b;
}

//
// --------- LootTemplate::LootGroup ---------
//



// Vote for an ongoing roll
void LootMgr::PlayerVote(Player* player, ObjectGuid const& lootTargetGuid, uint32 itemSlot, RollVote vote)
{
    LootBase* loot = FindLoot(player, lootTargetGuid);

    if (!loot)
    {
        sLog.outError("LootMgr::PlayerVote> Error cannot get loot object info!");
        return;
    }

    GroupLootRoll* roll = loot->GetRollForSlot(itemSlot);
    if (!roll)
    {
        sLog.outError("LootMgr::PlayerVote> Invalid itemSlot!");
        return;
    }

    roll->PlayerVote(player, vote);
}

// Get loot by object guid
// If target guid is not provided, try to find it by recipient or current player target
Loot* LootMgr::GetLoot(Player* player, ObjectGuid const& targetGuid) const
{
    //Loot* loot = nullptr;

    return nullptr;
}

// Get loot by object guid
// If target guid is not provided, try to find it by recipient or current player target
LootBase* LootMgr::FindLoot(Player* player, ObjectGuid const& targetGuid) const
{
    LootBase* loot = nullptr;
    ObjectGuid lguid;
    if (targetGuid.IsEmpty())
    {
        lguid = player->GetLootGuid();

        if (lguid.IsEmpty())
        {
            lguid = player->GetSelectionGuid();
            if (lguid.IsEmpty())
                return nullptr;
        }
    }
    else
        lguid = targetGuid;

    switch (lguid.GetHigh())
    {
        case HIGHGUID_GAMEOBJECT:
        {
            GameObject* gob = player->GetMap()->GetGameObject(lguid);

            // not check distance for GO in case owned GO (fishing bobber case, for example)
            if (gob)
                loot = gob->m_loot2.get();

            break;
        }
        case HIGHGUID_CORPSE:                               // remove insignia ONLY in BG
        {
            Corpse* bones = player->GetMap()->GetCorpse(lguid);

            if (bones)
                loot = bones->m_loot2.get();

            break;
        }
        case HIGHGUID_ITEM:
        {
            Item* item = player->GetItemByGuid(lguid);
            if (item && item->HasGeneratedLoot())
                loot = item->m_loot2.get();
            break;
        }
        case HIGHGUID_UNIT:
        {
            Creature* creature = player->GetMap()->GetCreature(lguid);

            if (creature)
                loot = creature->m_loot2.get();

            break;
        }
        default:
            return nullptr;                                         // unlootable type
    }

    return loot;
}

void LootMgr::CheckDropStats(ChatHandler& chat, uint32 amountOfCheck, uint32 lootId, std::string lootStore) const
{
    // choose correct loot template
    LootStore* store = &LootTemplates_Creature;
    if (lootStore != "creature")
    {
        if (lootStore == "gameobject")
            store = &LootTemplates_Gameobject;
        else if (lootStore == "fishing")
            store = &LootTemplates_Fishing;
        else if (lootStore == "item")
            store = &LootTemplates_Item;
        else if (lootStore == "pickpocketing")
            store = &LootTemplates_Pickpocketing;
        else if (lootStore == "skinning")
            store = &LootTemplates_Skinning;
        else if (lootStore == "disenchanting")
            store = &LootTemplates_Disenchant;
        else if (lootStore == "prospecting")
            store = &LootTemplates_Prospecting;
        else if (lootStore == "mail")
            store = &LootTemplates_Mail;
    }

    if (amountOfCheck < 1)
        amountOfCheck = 1;

    std::unique_ptr<Loot> loot = std::unique_ptr<Loot>(new Loot(LOOT_DEBUG));

    // get loot table for provided loot id
    LootTemplate const* lootTable = store->GetLootFor(lootId);
    if (!lootTable)
    {
        chat.PSendSysMessage("No table loot found for lootId(%u) in table loot table '%s'.", lootId, store->GetName());
        return;
    }

    // do the loot drop simulation
    std::unordered_map<uint32, uint32> itemStatsMap;
    for (uint32 i = 1; i <= amountOfCheck; ++i)
    {
        lootTable->Process(*loot, nullptr, *store, store->IsRatesAllowed());
        for (auto lootItem : loot->m_lootItems)
            ++itemStatsMap[lootItem->itemId];
        loot->Clear();
    }

    // sort the result
    auto comp = [](std::pair<uint32, uint32> const& a, std::pair<uint32, uint32> const& b) { return a.second > b.second; };
    std::set<std::pair<uint32, uint32>, decltype(comp)> sortedResult(
        itemStatsMap.begin(), itemStatsMap.end(), comp);

    // report the result in both chat client and console
    chat.PSendSysMessage("Results for %u drops simulation of loot id(%u) in %s:", amountOfCheck, lootId, store->GetName());
    sLog.outString("Results for %u drops simulation of loot id(%u) in %s:", amountOfCheck, lootId, store->GetName());
    std::stringstream ss;
    for (auto itemStat : sortedResult)
    {
        uint32 itemId = itemStat.first;
        ItemPrototype const* pProto = sItemStorage.LookupEntry<ItemPrototype >(itemId);
        if (!pProto)
            continue;

        std::string name = pProto->Name1;
        sObjectMgr.GetItemLocaleStrings(itemId, -1, &name);
        float computedStats = itemStat.second / float(amountOfCheck) * 100;
        ss.str("");
        ss.clear();
        ss << std::fixed << std::setprecision(4) << computedStats;
        ss << '%';
        chat.PSendSysMessage(LANG_ITEM_LIST_CHAT, itemId, itemId, name.c_str(), ss.str().c_str());
        sLog.outString("%6u - %-45s \tfound %6u/%-6u \tso %8s%% drop", itemStat.first, name.c_str(), itemStat.second, amountOfCheck, ss.str().c_str());
    }
}

LootBaseUPtr LootMgr::GenerateLoot(Player* player, Creature* lootTarget, LootType type)
{
    switch (type)
    {
        case LOOT_CORPSE:
            return std::move(LootBaseUPtr(new LootTypeCreatureSingle(*player, *lootTarget)));
            break;

        case LOOT_PICKPOCKETING:
            break;

        case LOOT_SKINNING:
            return std::move(LootBaseUPtr(new LootTypeSkinning(*player, *lootTarget)));
            break;

        case LOOT_DEBUG:
            break;
        default:
            break;
    }

    return nullptr;
}

LootBaseUPtr LootMgr::GenerateLoot(Player* player, GameObject* lootTarget, LootType type)
{
    switch (type)
    {
        case LOOT_FISHING:
        case LOOT_FISHING_FAIL:
        case LOOT_FISHINGHOLE:
            return std::move(LootBaseUPtr(new LootTypeFishing(*player, *lootTarget, type)));
            break;

        case  LOOT_CHEST:
            return std::move(LootBaseUPtr(new LootTypeChest(*player, *lootTarget)));

        default:
            break;
    }

    return nullptr;
}

LootBaseUPtr LootMgr::GenerateLoot(Player* player, Corpse* lootTarget)
{
    return std::move(LootBaseUPtr(new LootTypePlayerCorpse(*player, *lootTarget)));
}

LootBaseUPtr LootMgr::GenerateLoot(Player* player, Item* lootTarget, LootType type)
{
    return std::move(LootBaseUPtr(new LootTypeItem(*player, *lootTarget, type)));
}

