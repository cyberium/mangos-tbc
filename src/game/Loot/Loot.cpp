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
#include "Globals/ObjectAccessor.h"
#include "Entities/GameObject.h"
#include "Entities/Player.h"
#include "Entities/ObjectDefines.h"
#include "BattleGroundAV.h"
#include "World/World.h"
#include "Tools/Language.h"
#include "Globals/ObjectMgr.h"


//////////////////////////////////////////////////////////////////////////
// Base class of Loot system
//////////////////////////////////////////////////////////////////////////

void LootBase::PrintLootList() const
{
    for (auto lootItem : m_lootRule->GetFullContent())
    {
        sLog.outString("%s", lootItem->ToString().c_str());
    }
}

void LootBase::PrintLootList(ChatHandler& chat, WorldSession* session) const
{
    if (!session)
    {
        chat.SendSysMessage("Error you have to be in game for this command.");
        return;
    }

    if (m_lootRule->GetGoldAmount() == 0)
        chat.PSendSysMessage("Loot have no money");
    else
        chat.PSendSysMessage("Loot have (%u)coppers", m_lootRule->GetGoldAmount());

    auto lootItems = m_lootRule->GetFullContent();

    if (lootItems.empty())
    {
        chat.PSendSysMessage("Loot have no item.");
        return;
    }

    for (auto lootItem : lootItems)
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

void LootBase::SetPlayerLootingPose(Player& player, bool looting /*= true*/)
{
    ObjectGuid pGuid = player.GetObjectGuid();
    if (looting)
    {
        player.SetLootGuid(m_lootTarget->GetObjectGuid());    // used to keep track of what loot is opened for that player

        player.SetFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_LOOTING);
        if (m_lootTarget->IsGameObject())
            static_cast<GameObject*>(m_lootTarget)->SetInUse(true);
    }
    else
    {
        player.SetLootGuid(ObjectGuid());

        player.RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_LOOTING);
        if (m_lootTarget->IsGameObject())
            static_cast<GameObject*>(m_lootTarget)->SetInUse(false);
    }
}

// fill in the bytebuffer with loot content for specified player
void LootBase::BuildLootPacket(LootItemRightVec const& lootRights, ByteBuffer& buffer) const
{
    uint8 itemsShown = 0;

    // gold
    buffer << uint32(m_lootRule->GetGoldAmount());

    size_t count_pos = buffer.wpos();                            // pos of item count byte
    buffer << uint8(0);                                          // item count placeholder

    if (lootRights.empty())
        return;

    for (auto& lootRight : lootRights)
    {
        buffer << *lootRight.lootItem;
        buffer << uint8(lootRight.slotType);                      // 0 - get 1 - look only 2 - master selection
        ++itemsShown;
    }

    // update number of items shown
    buffer.put<uint8>(count_pos, itemsShown);
}

void LootBase::SendGold(Player& player)
{

}

void LootBase::SendReleaseFor(ObjectGuid const& guid)
{
    Player* plr = sObjectAccessor.FindPlayer(guid);
    if (plr && plr->GetSession())
        SendReleaseFor(*plr);
}

// no check for null pointer so it must be valid
void LootBase::SendReleaseFor(Player& plr)
{
    WorldPacket data(SMSG_LOOT_RELEASE_RESPONSE, (8 + 1));
    data << m_lootTarget->GetObjectGuid();
    data << uint8(1);
    plr.GetSession()->SendPacket(data);
}

void LootBase::SendReleaseForAll()
{
    auto work = [&](ObjectGuid const& guid)
    {
        SendReleaseFor(guid);
    };

    m_lootRule->DoWorkOnLooting(work);
}

bool LootBase::AutoStore(Player& player, bool broadcast /*= false*/, uint32 bag /*= NULL_BAG*/, uint32 slot /*= NULL_SLOT*/)
{
    bool result = true;
    LootItemRightVec lootRights;
    m_lootRule->HaveItemFor(player, &lootRights);

    for (auto& lootRight : lootRights)
    {
        LootItemSPtr& lootItem = lootRight.lootItem;

        if (lootRight.slotType != LOOT_SLOT_OWNER)
            continue; // already looted or not allowed

        ItemPosCountVec dest;
        InventoryResult msg = player.CanStoreNewItem(bag, slot, dest, lootItem->itemId, lootItem->count);
        if (msg != EQUIP_ERR_OK && slot != NULL_SLOT)
            msg = player.CanStoreNewItem(bag, NULL_SLOT, dest, lootItem->itemId, lootItem->count);
        if (msg != EQUIP_ERR_OK && bag != NULL_BAG)
            msg = player.CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, lootItem->itemId, lootItem->count);
        if (msg != EQUIP_ERR_OK)
        {
            player.SendEquipError(msg, nullptr, nullptr, lootItem->itemId);
            result = false;
            continue;
        }

        lootItem->pickedUpGuid.emplace(player.GetObjectGuid());

        Item* pItem = player.StoreNewItem(dest, lootItem->itemId, true, lootItem->randomPropertyId);
        player.SendNewItem(pItem, lootItem->count, false, false, broadcast);
        m_isChanged = true;
    }

    return result;
}

InventoryResult LootBase::SendItem(Player& target, uint32 itemSlot)
{
    LootItemSPtr lootItem = m_lootRule->GetLootItemInSlot(itemSlot);
    return SendItem(target, lootItem);
}

InventoryResult LootBase::SendItem(Player& target, LootItemSPtr lootItem)
{
    if (!lootItem)
        return EQUIP_ERR_ITEM_NOT_FOUND;

    bool playerGotItem = false;
    InventoryResult msg = EQUIP_ERR_CANT_DO_RIGHT_NOW;
    if (target.GetSession())
    {
        ItemPosCountVec dest;
        msg = target.CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, lootItem->itemId, lootItem->count);
        if (msg == EQUIP_ERR_OK)
        {
            Item* newItem = target.StoreNewItem(dest, lootItem->itemId, true, lootItem->randomPropertyId);
            NotifyItemRemoved(target, *lootItem);

            target.SendNewItem(newItem, uint32(lootItem->count), false, false, true);

            lootItem->pickedUpGuid.insert(target.GetObjectGuid());

            playerGotItem = true;
            m_isChanged = true;
        }
        else
            target.SendEquipError(msg, nullptr, nullptr, lootItem->itemId);
    }

    if (!playerGotItem)
    {
        // an error occurred player didn't received his loot
        lootItem->isBlocked = false;                                     // make the item available (was blocked since roll started)
        m_lootRule->OnFailedItemSent(target.GetObjectGuid(), *lootItem); // notify rule for sent failure
        //m_currentLooterGuid = target->GetObjectGuid();                 // change looter guid to let only him right to loot
        m_lootRule->SendAllowedLooter();                                 // update the looter right for client
    }
    else
    {
        if (m_lootRule->IsLootedForAll())
        {
            SendReleaseForAll();
            /*if (m_isChest)
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
            }*/
        }
        else if (!m_lootRule->HaveItemFor(target))
            SendReleaseFor(target);
        ForceLootAnimationClientUpdate();
    }
    return msg;
}

void LootBase::NotifyMoneyRemoved()
{
    // notify all players that are looting this that the money was removed
    auto work = [&](ObjectGuid const& guid)
    {
        Player* plr = ObjectAccessor::FindPlayer(guid);
        if (plr && plr->GetSession())
        {
            WorldPacket data(SMSG_LOOT_CLEAR_MONEY, 0);
            plr->GetSession()->SendPacket(data);
        }
    };

    m_lootRule->DoWorkOnLooting(work);
}

// notify all players that are looting this that the item was removed
void LootBase::NotifyItemRemoved(Player& player, LootItem& lootItem) const
{
    WorldPacket data(SMSG_LOOT_REMOVED, 1);
    data << uint8(lootItem.lootSlot);

    if (lootItem.freeForAll)
        player.GetSession()->SendPacket(data);
    else
    {
        auto work = [&](ObjectGuid const& guid)
        {
            Player* plr = ObjectAccessor::FindPlayer(guid);
            if (plr && plr->GetSession())
                plr->GetSession()->SendPacket(data);
        };

        m_lootRule->DoWorkOnLooting(work);
    }
}

// this will force server to update all client that is showing this object
// used to update players right to loot or sparkles animation
void LootBase::ForceLootAnimationClientUpdate() const
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
            //if (m_isChest)
            m_lootTarget->ForceValuesUpdateAtIndex(GAMEOBJECT_DYN_FLAGS);
            break;
        default:
            break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Skinning loot : using skinning rule.
// It doesn't matter if grouped or not.
//
// Player who did skinning spell will get first the loot windows.
//
// If player release (close the loot windows) with still item to loot,
// the loot will be available for anyone.
//////////////////////////////////////////////////////////////////////////

LootTypeSkinning::LootTypeSkinning(Player& player, Creature& lootTarget) : LootBase(LOOT_SKINNING, &lootTarget)
{
    // initialize loot rules using skinning rule
    m_lootRule = LootRuleUPtr(new SkinningRule(*this));
    m_lootRule->Initialize(player);

    // client need this type to display properly the loot window
    m_clientLootType = CLIENT_LOOT_PICKPOCKETING;

    // fill loot with dropped items
    CreatureInfo const* creatureInfo = lootTarget.GetCreatureInfo();
    m_lootRule->FillLoot(creatureInfo->SkinningLootId, LootTemplates_Skinning);
}

void LootTypeSkinning::ShowContentTo(Player& plr)
{
    LootItemRightVec lootRights;
    m_lootRule->HaveItemFor(plr, &lootRights);
    WorldPacket data(SMSG_LOOT_RESPONSE);
    data << m_lootTarget->GetObjectGuid();
    data << uint8(m_clientLootType);

    SetPlayerLootingPose(plr);
    m_lootRule->OnPlayerLooting(plr);
    BuildLootPacket(lootRights, data);

    if (m_lootTarget && (m_lootTarget->IsUnit() || m_lootTarget->IsGameObject()))
    {
        // Add a bit of time to let player pickup item if despawn timer is near
        static_cast<WorldObject*>(m_lootTarget)->InspectingLoot();
    }

    plr.SendDirectMessage(data);
}

void LootTypeSkinning::Release(Player& player, bool fromHandler /*= false*/)
{
    if (!m_lootRule->IsLooting(player.GetObjectGuid()))
        return;

    m_lootRule->OnRelease(player);
    SetPlayerLootingPose(player, false);
    Creature* creature = (Creature*)m_lootTarget;
    if (m_lootRule->IsLootedForAll())
    {
        creature->SetLootStatus(CREATURE_LOOT_STATUS_SKINNED);
    }
    else
    {
        // player released his skin so we can make it available for everyone
        if (creature->GetLootStatus() != CREATURE_LOOT_STATUS_SKIN_AVAILABLE)
        {
            creature->SetLootStatus(CREATURE_LOOT_STATUS_SKIN_AVAILABLE);
            ForceLootAnimationClientUpdate();
        }
    }

    if(!fromHandler)
        SendReleaseFor(player);
}


//////////////////////////////////////////////////////////////////////////
// Corpse single player loot : using single player rule
//
// Only when player is not grouped
// Items are only available for the player who was tapped
//////////////////////////////////////////////////////////////////////////

LootTypeCorpseSingle::LootTypeCorpseSingle(Player& player, Creature& lootTarget) : LootBase(LOOT_CORPSE, &lootTarget)
{
    // initialize loot rules using single player rule
    m_lootRule = LootRuleUPtr(new SinglePlayerRule(*this));
    m_lootRule->Initialize(player);

    // client need this type to display properly the loot window
    m_clientLootType = CLIENT_LOOT_CORPSE;

    // fill loot with dropped items
    CreatureInfo const* creatureInfo = lootTarget.GetCreatureInfo();
    m_lootRule->FillLoot(creatureInfo->LootId, LootTemplates_Creature);
}

void LootTypeCorpseSingle::ShowContentTo(Player& plr)
{
    LootItemRightVec lootRights;
    m_lootRule->HaveItemFor(plr, &lootRights);
    WorldPacket data(SMSG_LOOT_RESPONSE);
    data << m_lootTarget->GetObjectGuid();
    data << uint8(m_clientLootType);

    BuildLootPacket(lootRights, data);                           // fill the data with items contained in the loot (may be empty)
    SetPlayerLootingPose(plr, true);
    m_lootRule->OnPlayerLooting(plr);

    if (m_lootTarget && (m_lootTarget->IsUnit() || m_lootTarget->IsGameObject()))
    {
        // Add a bit of time to let player pickup item if despawn timer is near
        static_cast<WorldObject*>(m_lootTarget)->InspectingLoot();
    }

    plr.SendDirectMessage(data);
}

void LootTypeCorpseSingle::Release(Player& player, bool fromHandler /*= false*/)
{
    if (!m_lootRule->IsLooting(player.GetObjectGuid()))
        return;

    if (!fromHandler)
        SendReleaseFor(player);

    m_lootRule->OnRelease(player);
    SetPlayerLootingPose(player, false);

    if (!m_lootRule->HaveItemFor(player))
    {
        Creature* creature = (Creature*)m_lootTarget;
        creature->SetLootStatus(CREATURE_LOOT_STATUS_LOOTED);
        ForceLootAnimationClientUpdate();
    }
}

//////////////////////////////////////////////////////////////////////////
// Fishing loot : using single player rule.
// It doesn't matter if grouped or not.
//
// Only player who did fishing spell will get the loot windows.
//
// If player release (close the loot windows),
// the loot will be lost.
//////////////////////////////////////////////////////////////////////////

LootTypeFishing::LootTypeFishing(Player& player, GameObject& lootTarget, LootType type) : LootBase(type, &lootTarget)
{
    // initialize loot rules using single player rule
    m_lootRule = LootRuleUPtr(new SinglePlayerRule(*this));
    m_lootRule->Initialize(player);

    // client need this type to display properly the loot window
    m_clientLootType = CLIENT_LOOT_FISHING;

    // generate loot only if ready for open and spawned in world
    if (lootTarget.GetLootState() == GO_READY && lootTarget.IsSpawned())
    {
        if ((lootTarget.GetEntry() == BG_AV_OBJECTID_MINE_N || lootTarget.GetEntry() == BG_AV_OBJECTID_MINE_S))
        {
            if (BattleGround* bg = player.GetBattleGround())
                if (bg->GetTypeID() == BATTLEGROUND_AV)
                    if (!(((BattleGroundAV*)bg)->PlayerCanDoMineQuest(lootTarget.GetEntry(), player.GetTeam())))
                    {
                        return;
                    }
        }

        switch (type)
        {
            case LOOT_FISHING_FAIL:
            {
                // Entry 0 in fishing loot template used for store junk fish loot at fishing fail it junk allowed by config option
                // this is overwrite fishinghole loot for example
                m_lootRule->FillLoot(0, LootTemplates_Fishing);
                break;
            }
            case LOOT_FISHING:
            {
                uint32 zone, subzone;
                lootTarget.GetZoneAndAreaId(zone, subzone);
                // if subzone loot exist use it
                if (!m_lootRule->FillLoot(subzone, LootTemplates_Fishing, (subzone != zone)) && subzone != zone)
                    // else use zone loot (if zone diff. from subzone, must exist in like case)
                    m_lootRule->FillLoot(zone, LootTemplates_Fishing);
                break;
            }
            case LOOT_FISHINGHOLE:
            {
                if (uint32 lootid = lootTarget.GetGOInfo()->GetLootId())
                    m_lootRule->FillLoot(lootid, LootTemplates_Gameobject);
                break;
            }
            default:
            {
                sLog.outError("LootTypeFishing::LootTypeFishing> not supported loot type(%u)?", uint32(type));
                break;
            }
        }

        // important change gameobect state to activated (will wait loot release to be deleted)
        lootTarget.SetLootState(GO_ACTIVATED);
    }
}

void LootTypeFishing::ShowContentTo(Player& plr)
{
    LootItemRightVec lootRights;
    m_lootRule->HaveItemFor(plr, &lootRights);
    WorldPacket data(SMSG_LOOT_RESPONSE);
    data << m_lootTarget->GetObjectGuid();
    data << uint8(m_clientLootType);

    BuildLootPacket(lootRights, data);                           // fill the data with items contained in the loot (may be empty)
    m_lootRule->OnPlayerLooting(plr);
    SetPlayerLootingPose(plr);
    if (m_lootTarget && (m_lootTarget->IsUnit() || m_lootTarget->IsGameObject()))
    {
        // Add a bit of time to let player pickup item if despawn timer is near
        static_cast<WorldObject*>(m_lootTarget)->InspectingLoot();
    }

    plr.SendDirectMessage(data);
}

void LootTypeFishing::Release(Player& player, bool fromHandler /*= false*/)
{
    if (!m_lootRule->IsLooting(player.GetObjectGuid()))
        return;

    if (!fromHandler)
        SendReleaseFor(player);

    m_lootRule->OnRelease(player);
    SetPlayerLootingPose(player, false);
    auto gob = static_cast<GameObject*>(m_lootTarget);

    switch (gob->GetGoType())
    {
        case GAMEOBJECT_TYPE_FISHINGHOLE:
            // The fishing hole used once more
            gob->AddUse();                           // if the max usage is reached, will be despawned at next tick
            if (gob->GetUseCount() >= urand(gob->GetGOInfo()->fishinghole.minSuccessOpens, gob->GetGOInfo()->fishinghole.maxSuccessOpens))
            {
                gob->SetLootState(GO_JUST_DEACTIVATED);
            }
            else
                gob->SetLootState(GO_READY);
            break;

        default:
            gob->SetLootState(GO_JUST_DEACTIVATED);
            break;
    }
}

//////////////////////////////////////////////////////////////////////////
// Item loot : using single player rule.
// It doesn't matter if grouped or not.
//
// Only player who did opened the item will get the loot windows.
//
// If player release (close the loot windows),
// the loot will be lost for disenchanting and prospecting.
// Loot can however be saved in case of some container item.
//////////////////////////////////////////////////////////////////////////

LootTypeItem::LootTypeItem(Player& player, Item& lootTarget, LootType type) : LootBase(type, &lootTarget)
{
    // initialize loot rules using single player rule
    m_lootRule = LootRuleUPtr(new SinglePlayerRule(*this));
    m_lootRule->Initialize(player);

    // client need this type to display properly the loot window
    m_clientLootType = CLIENT_LOOT_PICKPOCKETING;

    // fill loot with dropped items
    switch (type)
    {
        case LOOT_DISENCHANTING:
            m_lootRule->FillLoot(lootTarget.GetProto()->DisenchantID, LootTemplates_Disenchant);
            lootTarget.SetLootState(ITEM_LOOT_TEMPORARY);
            break;

        case LOOT_PROSPECTING:
            m_lootRule->FillLoot(lootTarget.GetEntry(), LootTemplates_Prospecting);
            lootTarget.SetLootState(ITEM_LOOT_TEMPORARY);
            break;

        case LOOT_ITEM:
            m_lootRule->FillLoot(lootTarget.GetEntry(), LootTemplates_Item, lootTarget.GetProto()->MaxMoneyLoot == 0);
            m_lootRule->GenerateMoneyLoot(lootTarget.GetProto()->MinMoneyLoot, lootTarget.GetProto()->MaxMoneyLoot);
            lootTarget.SetLootState(ITEM_LOOT_CHANGED);
            break;

        // only case is when item is loaded
        default:
            m_lootType = LOOT_ITEM;
            lootTarget.SetLootState(ITEM_LOOT_CHANGED);
            break;
    }
}

void LootTypeItem::ShowContentTo(Player& plr)
{
    LootItemRightVec lootRights;
    m_lootRule->HaveItemFor(plr, &lootRights);
    WorldPacket data(SMSG_LOOT_RESPONSE);
    data << m_lootTarget->GetObjectGuid();
    data << uint8(m_clientLootType);

    BuildLootPacket(lootRights, data);                           // fill the data with items contained in the loot (may be empty)
    m_lootRule->OnPlayerLooting(plr);
    SetPlayerLootingPose(plr);
    if (m_lootTarget && (m_lootTarget->IsUnit() || m_lootTarget->IsGameObject()))
    {
        // Add a bit of time to let player pickup item if despawn timer is near
        static_cast<WorldObject*>(m_lootTarget)->InspectingLoot();
    }

    plr.SendDirectMessage(data);
}

void LootTypeItem::Release(Player& player, bool fromHandler /*= false*/)
{
    auto item = static_cast<Item*>(m_lootTarget);
    ForceLootAnimationClientUpdate();
    switch (m_lootType)
    {
        // temporary loot in stacking items, clear loot state, no auto loot move
        case LOOT_PROSPECTING:
        {
            uint32 count = item->GetCount();

            // >=5 checked in spell code, but will work for cheating cases also with removing from another stacks.
            if (count > 5)
                count = 5;

            // reset loot for allow repeat looting if stack > 5
            //Clear();
            item->SetLootState(ITEM_LOOT_REMOVED);
            player.DestroyItemCount(item, count, true);
            break;
        }

        // temporary loot, auto loot move
        case LOOT_DISENCHANTING:
        {
            if (!m_lootRule->HaveItemFor(player))
                AutoStore(player); // can be lost if no space
            //Clear();
            item->SetLootState(ITEM_LOOT_REMOVED);
            player.DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
            break;
        }

        // normal persistence loot
        case LOOT_ITEM:
        {
            // must be destroyed only if no loot
            if (!m_lootRule->HaveItemFor(player))
            {
                item->SetLootState(ITEM_LOOT_REMOVED);
                player.DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
            }
            break;
        }

        default:
        {
            // Should not happen
            break;
        }
    }

}

//////////////////////////////////////////////////////////////////////////
// Player corpse loot : using  group loot
// It doesn't matter if grouped or not.
//
// Money can be loot by anyone in the group
// Under threshold item will use round robin rules
//
// If player release (close the loot windows), items in it will be
// available for other group members (bypass round robin rules)
//////////////////////////////////////////////////////////////////////////

LootTypePlayerCorpse::LootTypePlayerCorpse(Player& player, Corpse& lootTarget) : LootBase(LOOT_INSIGNIA, &lootTarget)
{
    // initialize loot rules using single player rule
    m_lootRule = LootRuleUPtr(new SinglePlayerRule(*this));
    m_lootRule->Initialize(player);

    // client need this type to display properly the loot window
    m_clientLootType = CLIENT_LOOT_CORPSE;

    // fill loot with dropped items and gold
    if (!lootTarget.lootForBody && lootTarget.GetType() == CORPSE_BONES)
    {
        lootTarget.lootForBody = true;
        uint32 pLevel;
        if (Player* plr = sObjectAccessor.FindPlayer(lootTarget.GetOwnerGuid()))
            pLevel = plr->getLevel();
        else
            pLevel = player.getLevel(); // TODO:: not correct, need to save real player level in the corpse data in case of logout

        if (player.GetBattleGround()->GetTypeID() == BATTLEGROUND_AV)
            m_lootRule->FillLoot(0, LootTemplates_Creature);

        // It may need a better formula
        // Now it works like this: lvl10: ~6copper, lvl70: ~9silver
        uint32 gold = (uint32)(urand(50, 150) * 0.016f * pow(((float)pLevel) / 5.76f, 2.5f) * sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_MONEY));
        m_lootRule->SetGoldAmount(gold);
    }
}

void LootTypePlayerCorpse::ShowContentTo(Player& plr)
{
    LootItemRightVec lootRights;
    m_lootRule->HaveItemFor(plr, &lootRights);
    WorldPacket data(SMSG_LOOT_RESPONSE);
    data << m_lootTarget->GetObjectGuid();
    data << uint8(m_clientLootType);

    BuildLootPacket(lootRights, data);                           // fill the data with items contained in the loot (may be empty)
    m_lootRule->OnPlayerLooting(plr);
    SetPlayerLootingPose(plr);
    if (m_lootTarget && (m_lootTarget->IsUnit() || m_lootTarget->IsGameObject()))
    {
        // Add a bit of time to let player pickup item if despawn timer is near
        static_cast<WorldObject*>(m_lootTarget)->InspectingLoot();
    }

    plr.SendDirectMessage(data);
}

void LootTypePlayerCorpse::Release(Player& player, bool fromHandler /*= false*/)
{
    if (!m_lootRule->IsLooting(player.GetObjectGuid()))
        return;

    if (!fromHandler)
        SendReleaseFor(player);

    m_lootRule->OnRelease(player);
    SetPlayerLootingPose(player, false);
    auto corpse = static_cast<Corpse*>(m_lootTarget);
}
