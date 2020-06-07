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

#ifndef MANGOS_LOOTMGR_H
#define MANGOS_LOOTMGR_H

#include "LootDefines.h"

#include "ByteBuffer.h"
#include "Entities/ObjectGuid.h"
#include "Globals/SharedDefines.h"

#include <vector>
#include "Entities/Bag.h"
#include "LootGroupRoll.h"
#include "LootRules.h"
#include "LootStore.h"


#define LOOT_ROLL_TIMEOUT  (1*MINUTE*IN_MILLISECONDS)

class Player;
class GameObject;
class Corpse;
class Item;
class Group;
class LootStore;
class WorldObject;
class LootTemplate;
class Loot;

class LootTypeSkinning;
class LootRule;
class SkinningRule;

class WorldSession;
struct LootItem;
struct ItemPrototype;


//=====================================================


typedef std::vector<LootItem*> LootItemList;


//////////////////////////////////////////////////////////////////////////
// Old class should be removed
//////////////////////////////////////////////////////////////////////////
class Loot
{
    public:
        friend struct LootItem;
        friend class GroupLootRoll;
        friend class LootMgr;

        Loot(Player* player, Creature* creature, LootType type);
        Loot(Player* player, GameObject* gameObject, LootType type);
        Loot(Player* player, Corpse* corpse, LootType type);
        Loot(Player* player, Item* item, LootType type);
        Loot(Player* player, uint32 id, LootType type);
        Loot(Unit* unit, Item* item);
        Loot(LootType type);

        ~Loot();

        // Inserts the item into the loot (called by LootTemplate processors)
        void AddItem(LootStoreItem const& item);
        void AddItem(uint32 itemid, uint32 count, uint32 randomSuffix, int32 randomPropertyId);             // used in item.cpp to explicitly load a saved item
        bool AutoStore(Player* player, bool broadcast = false, uint32 bag = NULL_BAG, uint32 slot = NULL_SLOT);
        bool CanLoot(Player const* player);
        void ShowContentTo(Player* plr);
        void Update();
        bool IsChanged() const { return m_isChanged; }
        void Release(Player* player);
        void GetLootItemsListFor(Player* player, LootItemList& lootList);
        void SetGoldAmount(uint32 _gold);
        void SendGold(Player* player);
        void SendReleaseFor(Player* plr);
        bool IsItemAlreadyIn(uint32 itemId) const;
        void PrintLootList(ChatHandler& chat, WorldSession* session) const;
        bool HasLoot() const;
        uint32 GetGoldAmount() const { return m_gold; }
        LootType GetLootType() const { return m_lootType; }
        LootItem* GetLootItemInSlot(uint32 itemSlot);
        GroupLootRoll* GetRollForSlot(uint32 itemSlot);
        InventoryResult SendItem(Player* target, uint32 itemSlot);
        InventoryResult SendItem(Player* target, LootItem* lootItem);
        WorldObject const* GetLootTarget() const { return m_lootTarget; }
        ObjectGuid const& GetLootGuid() const { return m_guidTarget; }
        ObjectGuid const& GetMasterLootGuid() const { return m_masterOwnerGuid; }
        GuidSet const& GetOwnerSet() const { return m_ownerSet; }
        TimePoint const& GetCreateTime() const { return m_createTime; }

    private:
        Loot(): m_lootTarget(nullptr), m_itemTarget(nullptr), m_gold(0), m_maxSlot(0), m_lootType(),
            m_clientLootType(), m_lootMethod(), m_threshold(), m_maxEnchantSkill(0), m_haveItemOverThreshold(false),
            m_isChecked(false), m_isChest(false), m_isChanged(false), m_isFakeLoot(false)
        {}
        void Clear();
        bool IsLootedFor(Player const* player) const;
        bool IsLootedForAll() const;
        void SendReleaseFor(ObjectGuid const& guid);
        void SendReleaseForAll();
        void SendAllowedLooter();
        void NotifyMoneyRemoved();
        void NotifyItemRemoved(uint32 lootIndex);
        void NotifyItemRemoved(Player* player, uint32 lootIndex) const;
        void GroupCheck();
        void SetGroupLootRight(Player* player);
        void GenerateMoneyLoot(uint32 minAmount, uint32 maxAmount);
        bool FillLoot(uint32 loot_id, LootStore const& store, Player* lootOwner, bool personal, bool noEmptyError = false);
        void ForceLootAnimationClientUpdate() const;
        void SetPlayerIsLooting(Player* player);
        void SetPlayerIsNotLooting(Player* player);
        void GetLootContentFor(Player* player, ByteBuffer& buffer);
        uint32 GetLootStatusFor(Player const* player) const;
        bool IsLootOpenedBy(ObjectGuid const& playerGuid) const { return m_playersOpened.find(playerGuid) != m_playersOpened.end(); }

        // What is looted
        WorldObject*     m_lootTarget;
        Item*            m_itemTarget;
        ObjectGuid       m_guidTarget;

        LootItemList     m_lootItems;                     // store of the items contained in loot
        uint32           m_gold;                          // amount of money contained in loot
        uint32           m_maxSlot;                       // used to increment slot index and get total items count
        LootType         m_lootType;                      // internal loot type
        ClientLootType   m_clientLootType;                // client loot type
        LootMethod       m_lootMethod;                    // used to know what kind of check must be done at loot time
        ItemQualities    m_threshold;                     // group threshold for items
        ObjectGuid       m_masterOwnerGuid;               // master loot player or round robin owner
        ObjectGuid       m_currentLooterGuid;             // current player for under threshold items (Round Robin)
        GuidSet          m_ownerSet;                      // set of all player who have right to the loot
        uint32           m_maxEnchantSkill;               // used to know group right to use disenchant option
        bool             m_haveItemOverThreshold;         // if at least one item in the loot is over threshold
        bool             m_isChecked;                     // true if at least one player received the loot content
        bool             m_isChest;                       // chest type object have special loot right
        bool             m_isChanged;                     // true if at least one item is looted
        bool             m_isFakeLoot;                    // nothing to loot but will sparkle for empty windows
        GroupLootRollMap m_roll;                          // used if an item is under rolling
        GuidSet          m_playersLooting;                // player who opened loot windows
        GuidSet          m_playersOpened;                 // players that have released the corpse
        TimePoint        m_createTime;                    // create time (used to refill loot if need)
};

class LootMgr
{
    public:
        void PlayerVote(Player* player, ObjectGuid const& lootTargetGuid, uint32 itemSlot, RollVote vote);
        Loot* GetLoot(Player* player, ObjectGuid const& targetGuid = ObjectGuid()) const;
        LootBase* FindLoot(Player* player, ObjectGuid const& targetGuid) const;
        void CheckDropStats(ChatHandler& chat, uint32 amountOfCheck, uint32 lootId, std::string lootStore) const;

        LootBaseUPtr GenerateLoot(Player* player, Creature* lootTarget, LootType type);
        LootBaseUPtr GenerateLoot(Player* player, GameObject* lootTarget, LootType type);
        LootBaseUPtr GenerateLoot(Player* player, Corpse* lootTarget);
        LootBaseUPtr GenerateLoot(Player* player, Item* lootTarget, LootType type);
};

#define sLootMgr MaNGOS::Singleton<LootMgr>::Instance()
#endif
