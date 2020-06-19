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

#ifndef CMANGOS_LOOT_H
#define CMANGOS_LOOT_H
#include "Common.h"
#include "LootDefines.h"
#include "LootItem.h"
#include "LootStore.h"
#include "LootRules.h"
#include "Entities/Item.h"
#include "Entities/Bag.h"

class Object;
class Player;
class Corpse;
class GameObject;
class Creature;
class Item;



 //////////////////////////////////////////////////////////////////////////
 // Base class for loot
 //////////////////////////////////////////////////////////////////////////

class LootBase
{
    friend struct LootItem;
    friend class OldGroupLootRoll;
    friend class LootMgr;

public:
    LootBase(LootType lType, Object* lootTarget) :
        m_lootType(lType), m_isChanged(false), m_lootTarget(lootTarget) {}

    bool AddItem(LootStoreItem const& lootStoreItem) { return m_lootRule->AddItem(lootStoreItem); }
    void SetItemSent(LootItemSPtr lootItem, Player* player) { m_lootRule->SetItemSent(lootItem, player); }
    void BuildLootPacket(LootItemRightVec const& lootRights, ByteBuffer& buffer) const;
    virtual void Release(Player& player, bool fromHandler = false) = 0;
    void SetGoldAmount(uint32 amount) { m_lootRule->SetGoldAmount(amount); }
    void ShowContentTo(Player& plr);

    // used in item.cpp to explicitly load a saved item and should be defined in LootTypeItem
    virtual void AddSavedItem(uint32 itemid, uint32 count, uint32 randomSuffix, int32 randomPropertyId) {};


    // Send methods
    bool AutoStore(Player& player, bool broadcast = false, uint32 bag = NULL_BAG, uint32 slot = NULL_SLOT);
    void SendGold(Player& player);
    InventoryResult SendItem(Player& target, uint32 itemSlot);
    InventoryResult SendItem(Player& target, LootItemSPtr lootItem);
    Object* GetLootTarget() const { return m_lootTarget; }
    void ReleaseAll();

    // Getters
    virtual bool HaveLoot(Player const& player) const { return m_lootRule->HaveItemFor(player); }
    bool CanLootSlot(ObjectGuid const& guid, uint32 itemSlot) const { return m_lootRule->CanLootSlot(guid, itemSlot); }
    bool IsItemAlreadyIn(uint32 itemId) const { return m_lootRule->IsItemAlreadyIn(itemId); };
    LootType GetLootType() const { return m_lootType; }
    bool IsChanged() const { return m_isChanged; };
    GuidSet const& GetOwnerSet() const { return m_lootRule->GetOwnerSet(); }
    Player* GetOwner() const { return m_owner; }
    virtual uint32 GetGoldAmount() const { return m_lootRule->GetGoldAmount(); }
    bool GetLootFor(Player const& player, LootItemRightVec* lootItems) { return m_lootRule->HaveItemFor(player, lootItems); };
    LootMethod GetLootMethod() const { return m_lootRule->GetLootMethod(); }
    GroupLootRoll* GetRollForSlot(uint32 itemSlot) { return m_lootRule->GetRollForSlot(itemSlot); }

    virtual void Update(uint32 diff) {}

    // Utile
    void PrintLootList() const;
    void PrintLootList(ChatHandler& chat, WorldSession* session) const;


protected:
    void SetPlayerLootingPose(Player& player, bool looting = true);

    void NotifyMoneyRemoved();
    void NotifyItemRemoved(uint32 lootIndex);
    void NotifyItemRemoved(Player& player, LootItem& lootItem) const;
    void SendReleaseFor(ObjectGuid const& guid);
    void SendReleaseFor(Player& plr);
    void SendReleaseForAll();

    void ForceLootAnimationClientUpdate() const;

    Object* m_lootTarget;
    ClientLootType   m_clientLootType;
    LootType         m_lootType;
    LootRuleUPtr     m_lootRule;
    GuidSet          m_ownerSet;

private:
    Player* m_owner;
    bool m_isChanged;
};

class LootTypeSkinning : public LootBase
{
public:
    LootTypeSkinning(Player& player, Creature& lootTarget);
    void Release(Player& player, bool fromHandler /*= false*/) override;
};

class LootTypeCreature : public LootBase
{
public:
    LootTypeCreature(Player& player, Creature& lootTarget);
    void Release(Player& player, bool fromHandler = false) override;
    virtual void Update(uint32 diff) override { m_lootRule->Update(diff); };
};

class LootTypeGameObject : public LootBase
{
public:
    LootTypeGameObject(Player& player, GameObject& lootTarget);
    void Release(Player& player, bool fromHandler = false) override;
    virtual void Update(uint32 diff) override { m_lootRule->Update(diff); };
};

class LootTypeFishing : public LootBase
{
public:
    LootTypeFishing(Player& player, GameObject& lootTarget, LootType type);
    void Release(Player& player, bool fromHandler = false) override;
};

class LootTypeItem : public LootBase
{
public:
    LootTypeItem(Player& player, Item& lootTarget, LootType type);
    void Release(Player& player, bool fromHandler = false) override;


    virtual void AddSavedItem(uint32 itemid, uint32 count, uint32 randomSuffix, int32 randomPropertyId) override { m_lootRule->AddSavedItem(itemid, count, randomSuffix, randomPropertyId); };
};

class LootTypePlayerCorpse : public LootBase
{
public:
    LootTypePlayerCorpse(Player& player, Corpse& lootTarget);
    void Release(Player& player, bool fromHandler /*= false*/) override;
};

/*
class LootTypeOldChest : public LootBase
{
public:
    LootTypeOldChest(Player& player, GameObject& lootTarget);
    void Release(Player& player, bool fromHandler / *= false* /) override;
};*/

#endif
