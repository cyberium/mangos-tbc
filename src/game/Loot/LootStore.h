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

#ifndef CMANGOS_LOOT_STORE_ITEM_H
#define CMANGOS_LOOT_STORE_ITEM_H

#include "Common.h"
#include "LootDefines.h"

#include <set>
#include <unordered_map>

class Player;
class LootTemplate;
typedef std::set<uint32> LootIdSet;
typedef std::unordered_map<uint32, LootTemplate*> LootTemplateMap;

class LootStore
{
public:
    explicit LootStore(char const* name, char const* entryName, bool ratesAllowed)
        : m_name(name), m_entryName(entryName), m_ratesAllowed(ratesAllowed) {}
    virtual ~LootStore() { Clear(); }

    void Verify() const;

    void LoadAndCollectLootIds(LootIdSet& ids_set);
    void CheckLootRefs(LootIdSet* ref_set = nullptr) const; // check existence reference and remove it from ref_set
    void ReportUnusedIds(LootIdSet const& ids_set) const;
    void ReportNotExistedId(uint32 id) const;

    bool HaveLootFor(uint32 loot_id) const { return m_LootTemplates.find(loot_id) != m_LootTemplates.end(); }
    bool HaveQuestLootFor(uint32 loot_id) const;
    bool HaveQuestLootForPlayer(uint32 loot_id, Player* player) const;

    LootTemplate const* GetLootFor(uint32 loot_id) const;

    char const* GetName() const { return m_name; }
    char const* GetEntryName() const { return m_entryName; }
    bool IsRatesAllowed() const { return m_ratesAllowed; }
protected:
    void LoadLootTable();
    void Clear();
private:
    LootTemplateMap m_LootTemplates;
    char const* m_name;
    char const* m_entryName;
    bool m_ratesAllowed;
};

struct LootStoreItem
{
    uint32  itemid;                                         // id of the item
    float   chance;                                         // always positive, chance to drop for both quest and non-quest items, chance to be used for refs
    int32   mincountOrRef;                                  // mincount for drop items (positive) or minus referenced TemplateleId (negative)
    uint8   group       : 7;
    bool    needs_quest : 1;                                // quest drop (negative ChanceOrQuestChance in DB)
    uint8   maxcount    : 8;                                // max drop count for the item (mincountOrRef positive) or Ref multiplicator (mincountOrRef negative)
    uint16  conditionId : 16;                               // additional loot condition Id

    // Constructor, converting ChanceOrQuestChance -> (chance, needs_quest)
    // displayid is filled in IsValid() which must be called after
    LootStoreItem(uint32 _itemid, float _chanceOrQuestChance, int8 _group, uint16 _conditionId, int32 _mincountOrRef, uint8 _maxcount)
        : itemid(_itemid), chance(fabs(_chanceOrQuestChance)), mincountOrRef(_mincountOrRef),
          group(_group), needs_quest(_chanceOrQuestChance < 0), maxcount(_maxcount), conditionId(_conditionId)
    {}

    bool Roll(bool rate) const;                             // Checks if the entry takes it's chance (at loot generation)
    bool IsValid(LootStore const& store, uint32 entry) const;
    // Checks correctness of values
};

typedef std::unique_ptr<LootStoreItem> LootStoreItemUPtr;
typedef std::vector<LootStoreItem> LootStoreItemList;

class LootTemplate
{
    class  LootGroup;                                   // A set of loot definitions for items (refs are not allowed inside)
    typedef std::vector<LootGroup> LootGroups;

public:
    // Adds an entry to the group (at loading stage)
    void AddEntry(LootStoreItem& item);
    // Rolls for every item in the template and adds the rolled items the the loot
    void Process(Loot& loot, Player const* lootOwner, LootStore const& store, bool rate, uint8 groupId = 0) const;
    void Process(LootBase& loot, LootStore const& store, uint8 groupId = 0) const;
    // True if template includes at least 1 quest drop entry
    bool HasQuestDrop(LootTemplateMap const& store, uint8 groupId = 0) const;
    // True if template includes at least 1 quest drop for an active quest of the player
    bool HasQuestDropForPlayer(LootTemplateMap const& store, Player const* player, uint8 groupId = 0) const;
    // True if at least one player fulfill loot condition
    static bool PlayerOrGroupFulfilsCondition(const Loot& loot, Player const* lootOwner, uint16 conditionId);
    static bool FulfillConditions(LootBase const& loot, uint16 conditionId);
    // Checks integrity of the template
    void Verify(LootStore const& lootstore, uint32 id) const;
    void CheckLootRefs(LootIdSet* ref_set) const;
private:
    LootStoreItemList Entries;                          // not grouped only
    LootGroups        Groups;                           // groups have own (optimized) processing, grouped entries go there
};

extern LootStore LootTemplates_Creature;
extern LootStore LootTemplates_Fishing;
extern LootStore LootTemplates_Gameobject;
extern LootStore LootTemplates_Item;
extern LootStore LootTemplates_Mail;
extern LootStore LootTemplates_Pickpocketing;
extern LootStore LootTemplates_Skinning;
extern LootStore LootTemplates_Disenchant;
extern LootStore LootTemplates_Prospecting;

void LoadLootTemplates_Creature();
void LoadLootTemplates_Fishing();
void LoadLootTemplates_Gameobject();
void LoadLootTemplates_Item();
void LoadLootTemplates_Mail();
void LoadLootTemplates_Pickpocketing();
void LoadLootTemplates_Skinning();
void LoadLootTemplates_Disenchant();
void LoadLootTemplates_Prospecting();

void LoadLootTemplates_Reference();

inline void LoadLootTables()
{
    LoadLootTemplates_Creature();
    LoadLootTemplates_Fishing();
    LoadLootTemplates_Gameobject();
    LoadLootTemplates_Item();
    LoadLootTemplates_Mail();
    LoadLootTemplates_Pickpocketing();
    LoadLootTemplates_Skinning();
    LoadLootTemplates_Disenchant();
    LoadLootTemplates_Prospecting();

    LoadLootTemplates_Reference();
}

#endif