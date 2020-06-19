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

#ifndef CMANGOS_LOOT_RULES_H
#define CMANGOS_LOOT_RULES_H

#include "Common.h"
#include "LootDefines.h"
#include "Entities/ObjectGuid.h"
#include "LootItem.h"
#include "Globals/SharedDefines.h"
#include "LootGroupRoll.h"

class LootBase;

class LootStore;
class Player;

// LootItemRight used to retrieve right per player
struct LootItemRight
{
public:
    LootItemRight(LootItemSPtr _lootItem, LootSlotType _slotType) : lootItem(_lootItem), slotType(_slotType) {}
    LootItemSPtr lootItem;
    LootSlotType slotType;
};
typedef std::vector<LootItemRight> LootItemRightVec;
typedef std::unique_ptr<LootItemRightVec> LootItemRightVecUPtr;

//////////////////////////////////////////////////////////////////////////
// Base class for loot rules
//////////////////////////////////////////////////////////////////////////
class LootRule
{
public:
    LootRule(LootBase& loot) : m_loot(loot), m_lootItems(new LootItemVec()), m_gold(0) { m_lootItems->reserve(MAX_NR_LOOT_ITEMS); }

    virtual bool HaveItemFor(Player const& player, LootItemRightVec* lootItems = nullptr) const = 0;
    virtual uint32 GetGoldAmount() const { return m_gold; }
    void SetGoldAmount(uint32 amount) { m_gold = amount; }

    virtual void Initialize(Player& player);
    virtual bool CanLootSlot(ObjectGuid const& targetGuid, uint32 itemSlot);
    virtual bool IsLootedForAll() const;

    virtual bool IsEmpty() const { return m_lootItems->empty(); }
    void Reset();
    virtual bool AddItem(LootStoreItem const& lootStoreItem);
    void AddSavedItem(uint32 itemid, uint32 count, uint32 randomSuffix, int32 randomPropertyId);
    bool FillLoot(uint32 loot_id, LootStore const& store, bool noEmptyError = false);
    void GenerateMoneyLoot(uint32 minAmount, uint32 maxAmount);
    static bool IsEligibleForLoot(Player const& looter, LootBase const& loot);
    GuidSet const& GetOwnerSet() const { return m_ownerSet; }
    void SetItemSent(LootItemSPtr lootItem, Player* player);
    bool IsItemAlreadyIn(uint32 itemId) const;
    LootItemSPtr GetLootItemInSlot(uint32 itemSlot);
    virtual void SendAllowedLooter() {}
    virtual void OnFailedItemSent(ObjectGuid const& targetGuid, LootItem& lootItem) {}
    virtual void OnRelease(Player& plr);
    virtual void OnPlayerLooting(Player& plr);
    bool IsLooting(ObjectGuid const& guid) const { return m_playersLooting.find(guid) != m_playersLooting.end(); }
    virtual LootMethod GetLootMethod() const { return NOT_GROUP_TYPE_LOOT; }
    virtual GroupLootRoll* GetRollForSlot(uint32 itemSlot) { return nullptr; }
    virtual void Update(uint32 diff) {};

    template<typename T>
    void DoWorkOnFullGroup(T work) { for (auto owner : m_ownerSet) { work(owner); }; }

    template<typename T>
    void DoWorkOnLooting(T work) { for (auto lootingGuid : m_playersLooting) { work(lootingGuid); }; }

    LootItemVec const& GetFullContent() const { return *m_lootItems; }
    GuidSet const& GetLootingGuids() const { return m_playersLooting; }

protected:
    LootBase&        m_loot;
    uint32           m_gold;
    LootItemVecUPtr  m_lootItems;                           // store of the items contained in loot
    GuidSet          m_ownerSet;
    GuidSet          m_playersLooting;                      // player who opened loot windows
private:
};

typedef std::unique_ptr<LootRule> LootRuleUPtr;

// Skinning loot handling
class SkinningRule : public LootRule
{
public:
    SkinningRule(LootBase& loot) : LootRule(loot), m_isReleased(false) {}
    SkinningRule() = delete;

    virtual bool IsLootedForAll() const override;
    virtual void OnRelease(Player& plr) override;


    bool HaveItemFor(Player const& player, LootItemRightVec* lootItems = nullptr) const override;

private:
    bool m_isReleased;
};

// Single player rule loot handling
class SinglePlayerRule : public LootRule
{
public:
    SinglePlayerRule(LootBase& loot) : LootRule(loot) {}
    SinglePlayerRule() = delete;

    bool HaveItemFor(Player const& player, LootItemRightVec* lootItems = nullptr) const override;

private:

};

// Chest single player rules
class ChestSinglePlayerRule : public LootRule
{
public:
    ChestSinglePlayerRule(LootBase& loot) : LootRule(loot) {}
    ChestSinglePlayerRule() = delete;

    //bool CanLoot(Player const& player) const override;


    void Initialize(Player& player) override;
    virtual bool IsEmpty() const override;
    virtual bool IsLootedForAll() const override { return IsEmpty(); }

    bool HaveItemFor(Player const& player, LootItemRightVec* lootItems = nullptr) const override;
    void OnRelease(Player& plr) override;


private:
    bool m_groupLoot;
    LootMethod m_lootMethod;
    ItemQualities m_threshold;
    ObjectGuid m_masterOwnerGuid;
    ObjectGuid m_currentLooterGuid;
};

// Chest rules
class ChestRule : public LootRule
{
public:
    ChestRule(LootBase& loot) : LootRule(loot) {}
    ChestRule() = delete;

    void Initialize(Player& player) override;


    void OnRelease(Player& plr) override;


    //bool AddItem(LootStoreItem const& lootStoreItem) override;
private:

    ObjectGuid m_masterOwnerGuid;
};

class FreeForAllRule : public LootRule
{
public:
    FreeForAllRule(LootBase& loot) : LootRule(loot) {}
    FreeForAllRule() = delete;

    virtual void Initialize(Player& player) override;

    LootMethod GetLootMethod() const override { return FREE_FOR_ALL; }
    bool HaveItemFor(Player const& player, LootItemRightVec* lootItems = nullptr) const override;
};

class GroupLootRule : public FreeForAllRule
{
public:
    GroupLootRule(LootBase& loot) : FreeForAllRule(loot),
        m_currentLooterReleased(false), m_rollChecked(false) {}
    GroupLootRule() = delete;

    void Initialize(Player& player) override;

    LootMethod GetLootMethod() const override { return GROUP_LOOT; }

    bool AddItem(LootStoreItem const& lootStoreItem) override;
    bool HaveItemFor(Player const& player, LootItemRightVec* lootItems = nullptr) const override;
    void OnRelease(Player& plr) override;
    virtual void OnPlayerLooting(Player& plr) override;
    GroupLootRoll* GetRollForSlot(uint32 itemSlot);
    void Update(uint32 diff) override;
    virtual void SendAllowedLooter();

protected:
    ItemQualities   m_threshold;
    ObjectGuid      m_currentLooterGuid;
    bool            m_currentLooterReleased;
    bool            m_rollChecked;

    GroupLootRollMap m_rolls;
};

// only difference with GroupLoot is at Roll time for an item.
class NeedBeforeGreedRule : public GroupLootRule
{
public:
    NeedBeforeGreedRule(LootBase& loot) : GroupLootRule(loot) {}
    NeedBeforeGreedRule() = delete;

    LootMethod GetLootMethod() const override { return NEED_BEFORE_GREED; }
};

class RoundRobinRule : public GroupLootRule
{
public:
    RoundRobinRule(LootBase& loot) : GroupLootRule(loot) {}
    RoundRobinRule() = delete;

    LootMethod GetLootMethod() const override { return ROUND_ROBIN; }

    bool HaveItemFor(Player const& player, LootItemRightVec* lootItems = nullptr) const override;
    void OnRelease(Player& plr) override;
    void OnPlayerLooting(Player& plr) override { LootRule::OnPlayerLooting(plr); }
};

class MasterLootRule : public GroupLootRule
{
public:
    MasterLootRule(LootBase& loot) : GroupLootRule(loot) {}
    MasterLootRule() = delete;

    LootMethod GetLootMethod() const override { return MASTER_LOOT; }
    void Initialize(Player& player) override;

    bool AddItem(LootStoreItem const& lootStoreItem) override;
    bool HaveItemFor(Player const& player, LootItemRightVec* lootItems = nullptr) const override;
    void OnPlayerLooting(Player& plr) override;
    virtual void SendAllowedLooter() override;

private:
    ObjectGuid m_masterOwnerGuid;
};
#endif