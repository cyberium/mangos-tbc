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

    virtual bool CanLoot(Player const& player) const { return HaveItemFor(player); };
    virtual bool HaveItemFor(Player const& player, LootItemRightVec* lootItems = nullptr) const = 0;
    virtual uint32 GetGoldAmount() const { return m_gold; }
    void SetGoldAmount(uint32 amount) { m_gold = amount; }

    virtual void Initialize(Player& player);
    virtual bool CanLootSlot(ObjectGuid const& targetGuid, uint32 itemSlot);
    virtual bool IsLootedForAll() const;

    virtual bool AddItem(LootStoreItem const& lootStoreItem);
    void AddSavedItem(uint32 itemid, uint32 count, uint32 randomSuffix, int32 randomPropertyId);
    bool FillLoot(uint32 loot_id, LootStore const& store, bool noEmptyError = false);
    void GenerateMoneyLoot(uint32 minAmount, uint32 maxAmount);
    GuidSet const& GetOwnerSet() const { return m_ownerSet; }
    void SetItemSent(LootItemSPtr lootItem, Player* player);
    bool IsItemAlreadyIn(uint32 itemId) const;
    LootItemSPtr GetLootItemInSlot(uint32 itemSlot);
    virtual void SendAllowedLooter() {}
    virtual void OnFailedItemSent(ObjectGuid const& targetGuid, LootItem& lootItem) {}
    virtual void OnRelease(Player& plr);
    virtual void OnPlayerLooting(Player& plr);
    bool IsLooting(ObjectGuid const& guid) const { return m_playersLooting.find(guid) != m_playersLooting.end(); }

    template<typename T>
    void DoWorkOnFullGroup(T work) { for (auto owner : m_ownerSet) { work(owner); }; }

    template<typename T>
    void DoWorkOnLooting(T work) { for (auto lootingGuid : m_playersLooting) { work(lootingGuid); }; }

    LootItemVec const& GetFullContent() const { return *m_lootItems; }

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

    bool CanLoot(Player const& player) const override;


    void Initialize(Player& player) override;


    void OnRelease(Player& plr) override;


private:

    ObjectGuid m_masterOwnerGuid;
};

// Chest rules
class ChestRule : public LootRule
{
public:
    ChestRule(LootBase& loot) : LootRule(loot) {}
    ChestRule() = delete;

    bool CanLoot(Player const& player) const override;


    void Initialize(Player& player) override;


    void OnRelease(Player& plr) override;


    bool AddItem(LootStoreItem const& lootStoreItem) override;

private:

    ObjectGuid m_masterOwnerGuid;
};

class GroupLootBaseRule : public LootRule
{
public:
    GroupLootBaseRule(LootBase& loot) : LootRule(loot) {}
    GroupLootBaseRule() = delete;

protected:
    GuidSet          m_ownerSet;                      // set of all player who have right to the loot
};
#endif