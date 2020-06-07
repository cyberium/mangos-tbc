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

#include "LootStore.h"
#include "World/World.h"
#include "Globals/SharedDefines.h"
#include "Entities/Creature.h"
#include "Globals/Conditions.h"
#include "Globals/ObjectMgr.h"
#include "Loot.h"


static eConfigFloatValues const qualityToRate[MAX_ITEM_QUALITY] =
{
    CONFIG_FLOAT_RATE_DROP_ITEM_POOR,                       // ITEM_QUALITY_POOR
    CONFIG_FLOAT_RATE_DROP_ITEM_NORMAL,                     // ITEM_QUALITY_NORMAL
    CONFIG_FLOAT_RATE_DROP_ITEM_UNCOMMON,                   // ITEM_QUALITY_UNCOMMON
    CONFIG_FLOAT_RATE_DROP_ITEM_RARE,                       // ITEM_QUALITY_RARE
    CONFIG_FLOAT_RATE_DROP_ITEM_EPIC,                       // ITEM_QUALITY_EPIC
    CONFIG_FLOAT_RATE_DROP_ITEM_LEGENDARY,                  // ITEM_QUALITY_LEGENDARY
    CONFIG_FLOAT_RATE_DROP_ITEM_ARTIFACT,                   // ITEM_QUALITY_ARTIFACT
};

LootStore LootTemplates_Creature     ("creature_loot_template"     , "creature entry"                       , true);
LootStore LootTemplates_Disenchant   ("disenchant_loot_template"   , "item disenchant id"                   , true);
LootStore LootTemplates_Fishing      ("fishing_loot_template"      , "area id"                              , true);
LootStore LootTemplates_Gameobject   ("gameobject_loot_template"   , "gameobject lootid"                    , true);
LootStore LootTemplates_Item         ("item_loot_template"         , "item entry with ITEM_FLAG_LOOTABLE"   , true);
LootStore LootTemplates_Mail         ("mail_loot_template"         , "mail template id"                     , false);
LootStore LootTemplates_Pickpocketing("pickpocketing_loot_template", "creature pickpocket lootid"           , true);
LootStore LootTemplates_Prospecting  ("prospecting_loot_template"  , "item entry (ore)"                     , true);
LootStore LootTemplates_Reference    ("reference_loot_template"    , "reference id"                         , false);
LootStore LootTemplates_Skinning     ("skinning_loot_template"     , "creature skinning id"                 , true);

class LootTemplate::LootGroup                               // A set of loot definitions for items (refs are not allowed)
{
public:
    void AddEntry(LootStoreItem& item);                 // Adds an entry to the group (at loading stage)
    bool HasQuestDrop() const;                          // True if group includes at least 1 quest drop entry
    bool HasQuestDropForPlayer(Player const* player) const;
    // The same for active quests of the player
    void Process(Loot& loot, Player const* lootOwner) const; // Rolls an item from the group (if any) and adds the item to the loot
    void Process(LootBase& loot) const;
    float RawTotalChance() const;                       // Overall chance for the group (without equal chanced items)
    float TotalChance() const;                          // Overall chance for the group

    void Verify(LootStore const& lootstore, uint32 id, uint32 group_id) const;
    void CheckLootRefs(LootIdSet* ref_set) const;
private:
    LootStoreItemList ExplicitlyChanced;                // Entries with chances defined in DB
    LootStoreItemList EqualChanced;                     // Zero chances - every entry takes the same chance

    LootStoreItem const* Roll(Loot const& loot, Player const* lootOwner) const; // Rolls an item from the group, returns nullptr if all miss their chances
    LootStoreItem const* Roll(LootBase& loot) const;
};

// Remove all data and free all memory
void LootStore::Clear()
{
    for (LootTemplateMap::const_iterator itr = m_LootTemplates.begin(); itr != m_LootTemplates.end(); ++itr)
        delete itr->second;
    m_LootTemplates.clear();
}

// Checks validity of the loot store
// Actual checks are done within LootTemplate::Verify() which is called for every template
void LootStore::Verify() const
{
    for (const auto& m_LootTemplate : m_LootTemplates)
        m_LootTemplate.second->Verify(*this, m_LootTemplate.first);
}

// Loads a *_loot_template DB table into loot store
// All checks of the loaded template are called from here, no error reports at loot generation required
void LootStore::LoadLootTable()
{
    LootTemplateMap::const_iterator tab;
    uint32 count = 0;

    // Clearing store (for reloading case)
    Clear();

    //                                                 0      1     2                    3        4              5         6
    QueryResult* result = WorldDatabase.PQuery("SELECT entry, item, ChanceOrQuestChance, groupid, mincountOrRef, maxcount, condition_id FROM %s", GetName());

    if (result)
    {
        BarGoLink bar(result->GetRowCount());

        do
        {
            Field* fields = result->Fetch();
            bar.step();

            uint32 entry = fields[0].GetUInt32();
            uint32 item = fields[1].GetUInt32();
            float  chanceOrQuestChance = fields[2].GetFloat();
            uint8  group = fields[3].GetUInt8();
            int32  mincountOrRef = fields[4].GetInt32();
            uint32 maxcount = fields[5].GetUInt32();
            uint16 conditionId = fields[6].GetUInt16();

            if (maxcount > std::numeric_limits<uint8>::max())
            {
                sLog.outErrorDb("Table '%s' entry %u item %u: maxcount value (%u) to large. must be less than %u - skipped", GetName(), entry, item, maxcount, uint32(std::numeric_limits<uint8>::max()));
                continue;                                   // error already printed to log/console.
            }

            if (conditionId)
            {
                const ConditionEntry* condition = sConditionStorage.LookupEntry<ConditionEntry>(conditionId);
                if (!condition)
                {
                    sLog.outErrorDb("Table `%s` for entry %u, item %u has condition_id %u that does not exist in `conditions`, ignoring", GetName(), entry, item, uint32(conditionId));
                    continue;
                }

                if (mincountOrRef < 0 && !ConditionEntry::CanBeUsedWithoutPlayer(conditionId))
                {
                    sLog.outErrorDb("Table '%s' entry %u mincountOrRef %i < 0 and has condition %u that requires a player and is not supported, skipped", GetName(), entry, mincountOrRef, uint32(conditionId));
                    continue;
                }
            }

            LootStoreItem storeitem = LootStoreItem(item, chanceOrQuestChance, group, conditionId, mincountOrRef, maxcount);

            if (!storeitem.IsValid(*this, entry))           // Validity checks
                continue;

            // Looking for the template of the entry
            // often entries are put together
            if (m_LootTemplates.empty() || tab->first != entry)
            {
                // Searching the template (in case template Id changed)
                tab = m_LootTemplates.find(entry);
                if (tab == m_LootTemplates.end())
                {
                    std::pair< LootTemplateMap::iterator, bool > pr = m_LootTemplates.insert(LootTemplateMap::value_type(entry, new LootTemplate));
                    tab = pr.first;
                }
            }
            // else is empty - template Id and iter are the same
            // finally iter refers to already existing or just created <entry, LootTemplate>

            // Adds current row to the template
            tab->second->AddEntry(storeitem);
            ++count;
        } while (result->NextRow());

        delete result;

        Verify();                                           // Checks validity of the loot store

        sLog.outString(">> Loaded %u loot definitions (" SIZEFMTD " templates) from table %s", count, m_LootTemplates.size(), GetName());
        sLog.outString();
    }
    else
    {
        sLog.outString();
        sLog.outErrorDb(">> Loaded 0 loot definitions. DB table `%s` is empty.", GetName());
    }
}

bool LootStore::HaveQuestLootFor(uint32 loot_id) const
{
    LootTemplateMap::const_iterator itr = m_LootTemplates.find(loot_id);
    if (itr == m_LootTemplates.end())
        return false;

    // scan loot for quest items
    return itr->second->HasQuestDrop(m_LootTemplates);
}

bool LootStore::HaveQuestLootForPlayer(uint32 loot_id, Player* player) const
{
    LootTemplateMap::const_iterator tab = m_LootTemplates.find(loot_id);
    if (tab != m_LootTemplates.end())
        if (tab->second->HasQuestDropForPlayer(m_LootTemplates, player))
            return true;

    return false;
}

LootTemplate const* LootStore::GetLootFor(uint32 loot_id) const
{
    LootTemplateMap::const_iterator tab = m_LootTemplates.find(loot_id);

    if (tab == m_LootTemplates.end())
        return nullptr;

    return tab->second;
}

void LootStore::LoadAndCollectLootIds(LootIdSet& ids_set)
{
    LoadLootTable();

    for (LootTemplateMap::const_iterator tab = m_LootTemplates.begin(); tab != m_LootTemplates.end(); ++tab)
        ids_set.insert(tab->first);
}

void LootStore::CheckLootRefs(LootIdSet* ref_set) const
{
    for (const auto& m_LootTemplate : m_LootTemplates)
        m_LootTemplate.second->CheckLootRefs(ref_set);
}

void LootStore::ReportUnusedIds(LootIdSet const& ids_set) const
{
    // all still listed ids isn't referenced
    if (!ids_set.empty())
    {
        for (uint32 itr : ids_set)
            sLog.outErrorDb("Table '%s' entry %d isn't %s and not referenced from loot, and then useless.", GetName(), itr, GetEntryName());
        sLog.outString();
    }
}

void LootStore::ReportNotExistedId(uint32 id) const
{
    sLog.outErrorDb("Table '%s' entry %d (%s) not exist but used as loot id in DB.", GetName(), id, GetEntryName());
}

//
// --------- LootStoreItem ---------
//

// Checks if the entry (quest, non-quest, reference) takes it's chance (at loot generation)
// RATE_DROP_ITEMS is no longer used for all types of entries
bool LootStoreItem::Roll(bool rate) const
{
    if (chance >= 100.0f)
        return true;

    if (mincountOrRef < 0)                                  // reference case
        return roll_chance_f(chance * (rate ? sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_ITEM_REFERENCED) : 1.0f));

    if (needs_quest)
        return roll_chance_f(chance * (rate ? sWorld.getConfig(CONFIG_FLOAT_RATE_DROP_ITEM_QUEST) : 1.0f));

    ItemPrototype const* pProto = ObjectMgr::GetItemPrototype(itemid);

    float qualityModifier = pProto && rate ? sWorld.getConfig(qualityToRate[pProto->Quality]) : 1.0f;

    return roll_chance_f(chance * qualityModifier);
}

// Checks correctness of values
bool LootStoreItem::IsValid(LootStore const& store, uint32 entry) const
{
    if (group >= 1 << 7)                                    // it stored in 7 bit field
    {
        sLog.outErrorDb("Table '%s' entry %d item %d: group (%u) must be less %u - skipped", store.GetName(), entry, itemid, group, 1 << 7);
        return false;
    }

    if (mincountOrRef == 0)
    {
        sLog.outErrorDb("Table '%s' entry %d item %d: wrong mincountOrRef (%d) - skipped", store.GetName(), entry, itemid, mincountOrRef);
        return false;
    }

    if (mincountOrRef > 0)                                  // item (quest or non-quest) entry, maybe grouped
    {
        ItemPrototype const* proto = ObjectMgr::GetItemPrototype(itemid);
        if (!proto)
        {
            sLog.outErrorDb("Table '%s' entry %d item %d: item entry not listed in `item_template` - skipped", store.GetName(), entry, itemid);
            return false;
        }

        if (chance == 0 && group == 0)                      // Zero chance is allowed for grouped entries only
        {
            sLog.outErrorDb("Table '%s' entry %d item %d: equal-chanced grouped entry, but group not defined - skipped", store.GetName(), entry, itemid);
            return false;
        }

        if (chance != 0 && chance < 0.000001f)              // loot with low chance
        {
            sLog.outErrorDb("Table '%s' entry %d item %d: low chance (%f) - skipped", store.GetName(), entry, itemid, chance);
            return false;
        }

        if (maxcount < mincountOrRef)                       // wrong max count
        {
            sLog.outErrorDb("Table '%s' entry %d item %d: max count (%u) less that min count (%i) - skipped", store.GetName(), entry, itemid, uint32(maxcount), mincountOrRef);
            return false;
        }
    }
    else                                                    // mincountOrRef < 0
    {
        if (needs_quest)
        {
            sLog.outErrorDb("Table '%s' entry %d item %d: negative chance is given for a reference, skipped", store.GetName(), entry, itemid);
            return false;
        }
        if (chance == 0)                               // no chance for the reference
        {
            sLog.outErrorDb("Table '%s' entry %d item %d: zero chance is given for a reference, reference will never be used, skipped", store.GetName(), entry, itemid);
            return false;
        }
    }
    return true;                                            // Referenced template existence is checked at whole store level
}

// Adds an entry to the group (at loading stage)
void LootTemplate::LootGroup::AddEntry(LootStoreItem& item)
{
    if (item.chance != 0)
        ExplicitlyChanced.push_back(item);
    else
        EqualChanced.push_back(item);
}

// Rolls an item from the group, returns nullptr if all miss their chances
LootStoreItem const* LootTemplate::LootGroup::Roll(LootBase& loot) const
{
    if (!ExplicitlyChanced.empty())                         // First explicitly chanced entries are checked
    {
        std::vector <LootStoreItem const*> lootStoreItemVector; // we'll use new vector to make easy the randomization

        // fill the new vector with correct pointer to our item list
        for (auto& itr : ExplicitlyChanced)
            lootStoreItemVector.push_back(&itr);

        // randomize the new vector
        random_shuffle(lootStoreItemVector.begin(), lootStoreItemVector.end());

        float chance = rand_chance_f();

        // as the new vector is randomized we can start from first element and stop at first one that meet the condition
        for (std::vector <LootStoreItem const*>::const_iterator itr = lootStoreItemVector.begin(); itr != lootStoreItemVector.end(); ++itr)
        {
            LootStoreItem const* lsi = *itr;

            if (lsi->conditionId && !LootTemplate::FulfillConditions(loot, lsi->conditionId))
            {
                sLog.outDebug("In explicit chance -> This item cannot be added! (%u)", lsi->itemid);
                continue;
            }

            if (lsi->chance >= 100.0f)
                return lsi;

            chance -= lsi->chance;
            if (chance < 0)
                return lsi;
        }
    }

    if (!EqualChanced.empty())                              // If nothing selected yet - an item is taken from equal-chanced part
    {
        std::vector <LootStoreItem const*> lootStoreItemVector; // we'll use new vector to make easy the randomization

        // fill the new vector with correct pointer to our item list
        for (auto& itr : EqualChanced)
            lootStoreItemVector.push_back(&itr);

        // randomize the new vector
        random_shuffle(lootStoreItemVector.begin(), lootStoreItemVector.end());

        // as the new vector is randomized we can start from first element and stop at first one that meet the condition
        for (std::vector <LootStoreItem const*>::const_iterator itr = lootStoreItemVector.begin(); itr != lootStoreItemVector.end(); ++itr)
        {
            LootStoreItem const* lsi = *itr;

            //check if we already have that item in the loot list
            if (loot.IsItemAlreadyIn(lsi->itemid))
            {
                // the item is already looted, let's give a 50%  chance to pick another one
                uint32 chance = urand(0, 1);

                if (chance)
                    continue;                               // pass this item
            }

            if (lsi->conditionId && !LootTemplate::FulfillConditions(loot, lsi->conditionId))
            {
                sLog.outDebug("In equal chance -> This item cannot be added! (%u)", lsi->itemid);
                continue;
            }
            return lsi;
        }
    }

    return nullptr;                                            // Empty drop from the group
}

// Rolls an item from the group, returns nullptr if all miss their chances
LootStoreItem const* LootTemplate::LootGroup::Roll(Loot const& loot, Player const* lootOwner) const
{
    if (!ExplicitlyChanced.empty())                         // First explicitly chanced entries are checked
    {
        std::vector <LootStoreItem const*> lootStoreItemVector; // we'll use new vector to make easy the randomization

        // fill the new vector with correct pointer to our item list
        for (auto& itr : ExplicitlyChanced)
            lootStoreItemVector.push_back(&itr);

        // randomize the new vector
        random_shuffle(lootStoreItemVector.begin(), lootStoreItemVector.end());

        float chance = rand_chance_f();

        // as the new vector is randomized we can start from first element and stop at first one that meet the condition
        for (std::vector <LootStoreItem const*>::const_iterator itr = lootStoreItemVector.begin(); itr != lootStoreItemVector.end(); ++itr)
        {
            LootStoreItem const* lsi = *itr;

            if (lsi->conditionId && lootOwner && !LootTemplate::PlayerOrGroupFulfilsCondition(loot, lootOwner, lsi->conditionId))
            {
                sLog.outDebug("In explicit chance -> This item cannot be added! (%u)", lsi->itemid);
                continue;
            }

            if (lsi->chance >= 100.0f)
                return lsi;

            chance -= lsi->chance;
            if (chance < 0)
                return lsi;
        }
    }

    if (!EqualChanced.empty())                              // If nothing selected yet - an item is taken from equal-chanced part
    {
        std::vector <LootStoreItem const*> lootStoreItemVector; // we'll use new vector to make easy the randomization

        // fill the new vector with correct pointer to our item list
        for (auto& itr : EqualChanced)
            lootStoreItemVector.push_back(&itr);

        // randomize the new vector
        random_shuffle(lootStoreItemVector.begin(), lootStoreItemVector.end());

        // as the new vector is randomized we can start from first element and stop at first one that meet the condition
        for (std::vector <LootStoreItem const*>::const_iterator itr = lootStoreItemVector.begin(); itr != lootStoreItemVector.end(); ++itr)
        {
            LootStoreItem const* lsi = *itr;

            //check if we already have that item in the loot list
            if (loot.IsItemAlreadyIn(lsi->itemid))
            {
                // the item is already looted, let's give a 50%  chance to pick another one
                uint32 chance = urand(0, 1);

                if (chance)
                    continue;                               // pass this item
            }

            if (lsi->conditionId && lootOwner && !LootTemplate::PlayerOrGroupFulfilsCondition(loot, lootOwner, lsi->conditionId))
            {
                sLog.outDebug("In equal chance -> This item cannot be added! (%u)", lsi->itemid);
                continue;
            }
            return lsi;
        }
    }

    return nullptr;                                            // Empty drop from the group
}

// True if group includes at least 1 quest drop entry
bool LootTemplate::LootGroup::HasQuestDrop() const
{
    for (auto i : ExplicitlyChanced)
        if (i.needs_quest)
            return true;
    for (auto i : EqualChanced)
        if (i.needs_quest)
            return true;
    return false;
}

// True if group includes at least 1 quest drop entry for active quests of the player
bool LootTemplate::LootGroup::HasQuestDropForPlayer(Player const* player) const
{
    for (auto i : ExplicitlyChanced)
        if (player->HasQuestForItem(i.itemid))
            return true;
    for (auto i : EqualChanced)
        if (player->HasQuestForItem(i.itemid))
            return true;
    return false;
}

// Rolls an item from the group (if any takes its chance) and adds the item to the loot
void LootTemplate::LootGroup::Process(LootBase& loot) const
{
    LootStoreItem const* item = Roll(loot);
    if (item != nullptr)
        loot.AddItem(*item);
}

// Rolls an item from the group (if any takes its chance) and adds the item to the loot
void LootTemplate::LootGroup::Process(Loot& loot, Player const* lootOwner) const
{
    LootStoreItem const* item = Roll(loot, lootOwner);
    if (item != nullptr)
        loot.AddItem(*item);
}

// Overall chance for the group without equal chanced items
float LootTemplate::LootGroup::RawTotalChance() const
{
    float result = 0;

    for (auto i : ExplicitlyChanced)
        if (!i.needs_quest)
            result += i.chance;

    return result;
}

// Overall chance for the group
float LootTemplate::LootGroup::TotalChance() const
{
    float result = RawTotalChance();

    if (!EqualChanced.empty() && result < 100.0f)
        return 100.0f;

    return result;
}

void LootTemplate::LootGroup::Verify(LootStore const& lootstore, uint32 id, uint32 group_id) const
{
    float chance = RawTotalChance();
    if (chance > 101.0f)                                    // TODO: replace with 100% when DBs will be ready
    {
        sLog.outErrorDb("Table '%s' entry %u group %d has total chance > 100%% (%f)", lootstore.GetName(), id, group_id, chance);
    }

    if (chance >= 100.0f && !EqualChanced.empty())
    {
        sLog.outErrorDb("Table '%s' entry %u group %d has items with chance=0%% but group total chance >= 100%% (%f)", lootstore.GetName(), id, group_id, chance);
    }
}

void LootTemplate::LootGroup::CheckLootRefs(LootIdSet* ref_set) const
{
    for (auto ieItr : ExplicitlyChanced)
    {
        if (ieItr.mincountOrRef < 0)
        {
            if (!LootTemplates_Reference.GetLootFor(-ieItr.mincountOrRef))
                LootTemplates_Reference.ReportNotExistedId(-ieItr.mincountOrRef);
            else if (ref_set)
                ref_set->erase(-ieItr.mincountOrRef);
        }
    }

    for (auto ieItr : EqualChanced)
    {
        if (ieItr.mincountOrRef < 0)
        {
            if (!LootTemplates_Reference.GetLootFor(-ieItr.mincountOrRef))
                LootTemplates_Reference.ReportNotExistedId(-ieItr.mincountOrRef);
            else if (ref_set)
                ref_set->erase(-ieItr.mincountOrRef);
        }
    }
}

//
// --------- LootTemplate ---------
//

// Adds an entry to the group (at loading stage)
void LootTemplate::AddEntry(LootStoreItem& item)
{
    if (item.group > 0 && item.mincountOrRef > 0)           // Group
    {
        if (item.group >= Groups.size())
            Groups.resize(item.group);                      // Adds new group the the loot template if needed
        Groups[item.group - 1].AddEntry(item);              // Adds new entry to the group
    }
    else                                                    // Non-grouped entries and references are stored together
        Entries.push_back(item);
}

// Rolls for every item in the template and adds the rolled items the the loot
void LootTemplate::Process(Loot& loot, Player const* lootOwner, LootStore const& store, bool rate, uint8 groupId) const
{
    if (groupId)                                            // Group reference uses own processing of the group
    {
        if (groupId > Groups.size())
            return;                                         // Error message already printed at loading stage

        Groups[groupId - 1].Process(loot, lootOwner);
        return;
    }

    // Rolling non-grouped items
    for (auto Entrie : Entries)
    {
        // Check condition
        if (Entrie.conditionId && lootOwner && !PlayerOrGroupFulfilsCondition(loot, lootOwner, Entrie.conditionId))
            continue;

        if (!Entrie.Roll(rate))
            continue;                                       // Bad luck for the entry

        if (Entrie.mincountOrRef < 0)                           // References processing
        {
            LootTemplate const* Referenced = LootTemplates_Reference.GetLootFor(-Entrie.mincountOrRef);

            if (!Referenced)
                continue;                                   // Error message already printed at loading stage

            for (uint32 loop = 0; loop < Entrie.maxcount; ++loop) // Ref multiplicator
                Referenced->Process(loot, lootOwner, store, rate, Entrie.group);
        }
        else                                                // Plain entries (not a reference, not grouped)
            loot.AddItem(Entrie);                               // Chance is already checked, just add
    }

    // Now processing groups
    for (const auto& Group : Groups)
        Group.Process(loot, lootOwner);
}

// Rolls for every item in the template and adds the rolled items the the loot
void LootTemplate::Process(LootBase& loot, LootStore const& store, uint8 groupId /*= 0*/) const
{
    if (groupId)                                            // Group reference uses own processing of the group
    {
        if (groupId > Groups.size())
            return;                                         // Error message already printed at loading stage

        Groups[groupId - 1].Process(loot);
        return;
    }

    // Rolling non-grouped items
    for (auto Entrie : Entries)
    {
        // Check condition
        if (Entrie.conditionId && !FulfillConditions(loot, Entrie.conditionId))
            continue;

        if (!Entrie.Roll(store.IsRatesAllowed()))
            continue;                                       // Bad luck for the entry

        if (Entrie.mincountOrRef < 0)                       // References processing
        {
            LootTemplate const* Referenced = LootTemplates_Reference.GetLootFor(-Entrie.mincountOrRef);

            if (!Referenced)
                continue;                                   // Error message already printed at loading stage

            for (uint32 loop = 0; loop < Entrie.maxcount; ++loop) // Ref multiplicator
                Referenced->Process(loot, store, Entrie.group);
        }
        else
        {                                                   // Plain entries (not a reference, not grouped)
            if (!loot.AddItem(Entrie))                      // Chance is already checked, just add
                return;                                     // max item count per drop reached
        }
    }

    // Now processing groups
    for (const auto& Group : Groups)
        Group.Process(loot);
}

// True if template includes at least 1 quest drop entry
bool LootTemplate::HasQuestDrop(LootTemplateMap const& store, uint8 groupId) const
{
    if (groupId)                                            // Group reference
    {
        if (groupId > Groups.size())
            return false;                                   // Error message [should be] already printed at loading stage
        return Groups[groupId - 1].HasQuestDrop();
    }

    for (auto Entrie : Entries)
    {
        if (Entrie.mincountOrRef < 0)                           // References
        {
            LootTemplateMap::const_iterator Referenced = store.find(-Entrie.mincountOrRef);
            if (Referenced == store.end())
                continue;                                   // Error message [should be] already printed at loading stage
            if (Referenced->second->HasQuestDrop(store, Entrie.group))
                return true;
        }
        else if (Entrie.needs_quest)
            return true;                                    // quest drop found
    }

    // Now processing groups
    for (const auto& Group : Groups)
        if (Group.HasQuestDrop())
            return true;

    return false;
}

// True if template includes at least 1 quest drop for an active quest of the player
bool LootTemplate::HasQuestDropForPlayer(LootTemplateMap const& store, Player const* player, uint8 groupId) const
{
    if (groupId)                                            // Group reference
    {
        if (groupId > Groups.size())
            return false;                                   // Error message already printed at loading stage
        return Groups[groupId - 1].HasQuestDropForPlayer(player);
    }

    // Checking non-grouped entries
    for (auto Entrie : Entries)
    {
        if (Entrie.mincountOrRef < 0)                           // References processing
        {
            LootTemplateMap::const_iterator Referenced = store.find(-Entrie.mincountOrRef);
            if (Referenced == store.end())
                continue;                                   // Error message already printed at loading stage
            if (Referenced->second->HasQuestDropForPlayer(store, player, Entrie.group))
                return true;
        }
        else if (player->HasQuestForItem(Entrie.itemid))
            return true;                                    // active quest drop found
    }

    // Now checking groups
    for (const auto& Group : Groups)
        if (Group.HasQuestDropForPlayer(player))
            return true;

    return false;
}

bool LootTemplate::PlayerOrGroupFulfilsCondition(const Loot& loot, Player const* lootOwner, uint16 conditionId)
{
    Map* map = lootOwner->IsInWorld() ? lootOwner->GetMap() : loot.GetLootTarget()->GetMap(); // if neither succeeds, we have a design problem
    auto& ownerSet = loot.GetOwnerSet();
    // optimization - no need to look up when player is solo
    if (ownerSet.size() <= 1)
        return sObjectMgr.IsConditionSatisfied(conditionId, lootOwner, map, loot.GetLootTarget(), CONDITION_FROM_REFERING_LOOT);

    for (const ObjectGuid& guid : ownerSet)
        if (Player* player = map->GetPlayer(guid))
            if (sObjectMgr.IsConditionSatisfied(conditionId, player, map, loot.GetLootTarget(), CONDITION_FROM_REFERING_LOOT))
                return true;

    return false;
}

bool LootTemplate::FulfillConditions(LootBase const& loot, uint16 conditionId)
{
    auto lootTarget = loot.GetLootTarget();
    if (lootTarget->isType(TYPEMASK_WORLDOBJECT))
    {
        WorldObject* target = static_cast<WorldObject*>(lootTarget);
        Map* map = target->GetMap();
        auto& ownerSet = loot.GetOwnerSet();

        for (const ObjectGuid& guid : ownerSet)
            if (Player* player = map->GetPlayer(guid))
                if (sObjectMgr.IsConditionSatisfied(conditionId, player, map, target, CONDITION_FROM_REFERING_LOOT))
                    return true;
    }

    return false;
}

// Checks integrity of the template
void LootTemplate::Verify(LootStore const& lootstore, uint32 id) const
{
    // Checking group chances
    for (uint32 i = 0; i < Groups.size(); ++i)
        Groups[i].Verify(lootstore, id, i + 1);

    // TODO: References validity checks
}

void LootTemplate::CheckLootRefs(LootIdSet* ref_set) const
{
    for (auto Entrie : Entries)
    {
        if (Entrie.mincountOrRef < 0)
        {
            if (!LootTemplates_Reference.GetLootFor(-Entrie.mincountOrRef))
                LootTemplates_Reference.ReportNotExistedId(-Entrie.mincountOrRef);
            else if (ref_set)
                ref_set->erase(-Entrie.mincountOrRef);
        }
    }

    for (const auto& Group : Groups)
        Group.CheckLootRefs(ref_set);
}

void LoadLootTemplates_Creature()
{
    LootIdSet ids_set, ids_setUsed;
    LootTemplates_Creature.LoadAndCollectLootIds(ids_set);

    // remove real entries and check existence loot
    for (uint32 i = 1; i < sCreatureStorage.GetMaxEntry(); ++i)
    {
        if (CreatureInfo const* cInfo = sCreatureStorage.LookupEntry<CreatureInfo>(i))
        {
            if (uint32 lootid = cInfo->LootId)
            {
                if (ids_set.find(lootid) == ids_set.end())
                    LootTemplates_Creature.ReportNotExistedId(lootid);
                else
                    ids_setUsed.insert(lootid);
            }
        }
    }
    for (uint32 itr : ids_setUsed)
        ids_set.erase(itr);

    // for alterac valley we've defined Player-loot inside creature_loot_template id=0
    // this hack is used, so that we won't need to create an extra table player_loot_template for just one case
    ids_set.erase(0);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Creature.ReportUnusedIds(ids_set);
}

void LoadLootTemplates_Disenchant()
{
    LootIdSet ids_set, ids_setUsed;
    LootTemplates_Disenchant.LoadAndCollectLootIds(ids_set);

    // remove real entries and check existence loot
    for (uint32 i = 1; i < sItemStorage.GetMaxEntry(); ++i)
    {
        if (ItemPrototype const* proto = sItemStorage.LookupEntry<ItemPrototype>(i))
        {
            if (uint32 lootid = proto->DisenchantID)
            {
                if (ids_set.find(lootid) == ids_set.end())
                    LootTemplates_Disenchant.ReportNotExistedId(lootid);
                else
                    ids_setUsed.insert(lootid);
            }
        }
    }
    for (uint32 itr : ids_setUsed)
        ids_set.erase(itr);
    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Disenchant.ReportUnusedIds(ids_set);
}

void LoadLootTemplates_Fishing()
{
    LootIdSet ids_set;
    LootTemplates_Fishing.LoadAndCollectLootIds(ids_set);

    // remove real entries and check existence loot
    for (uint32 i = 1; i < sAreaStore.GetNumRows(); ++i)
    {
        if (AreaTableEntry const* areaEntry = sAreaStore.LookupEntry(i))
            if (ids_set.find(areaEntry->ID) != ids_set.end())
                ids_set.erase(areaEntry->ID);
    }

    // by default (look config options) fishing at fail provide junk loot, entry 0 use for store this loot
    ids_set.erase(0);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Fishing.ReportUnusedIds(ids_set);
}

void LoadLootTemplates_Gameobject()
{
    LootIdSet ids_set, ids_setUsed;
    LootTemplates_Gameobject.LoadAndCollectLootIds(ids_set);

    // remove real entries and check existence loot
    for (SQLStorageBase::SQLSIterator<GameObjectInfo> itr = sGOStorage.getDataBegin<GameObjectInfo>(); itr < sGOStorage.getDataEnd<GameObjectInfo>(); ++itr)
    {
        if (uint32 lootid = itr->GetLootId())
        {
            if (ids_set.find(lootid) == ids_set.end())
                LootTemplates_Gameobject.ReportNotExistedId(lootid);
            else
                ids_setUsed.insert(lootid);
        }
    }
    for (uint32 itr : ids_setUsed)
        ids_set.erase(itr);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Gameobject.ReportUnusedIds(ids_set);
}

void LoadLootTemplates_Item()
{
    LootIdSet ids_set;
    LootTemplates_Item.LoadAndCollectLootIds(ids_set);

    // remove real entries and check existence loot
    for (uint32 i = 1; i < sItemStorage.GetMaxEntry(); ++i)
    {
        if (ItemPrototype const* proto = sItemStorage.LookupEntry<ItemPrototype>(i))
        {
            if (!(proto->Flags & ITEM_FLAG_HAS_LOOT))
                continue;

            if (ids_set.find(proto->ItemId) != ids_set.end() || proto->MaxMoneyLoot > 0)
                ids_set.erase(proto->ItemId);
            // wdb have wrong data cases, so skip by default
            else if (!sLog.HasLogFilter(LOG_FILTER_DB_STRICTED_CHECK))
                LootTemplates_Item.ReportNotExistedId(proto->ItemId);
        }
    }

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Item.ReportUnusedIds(ids_set);
}

void LoadLootTemplates_Pickpocketing()
{
    LootIdSet ids_set, ids_setUsed;
    LootTemplates_Pickpocketing.LoadAndCollectLootIds(ids_set);

    // remove real entries and check existence loot
    for (uint32 i = 1; i < sCreatureStorage.GetMaxEntry(); ++i)
    {
        if (CreatureInfo const* cInfo = sCreatureStorage.LookupEntry<CreatureInfo>(i))
        {
            if (uint32 lootid = cInfo->PickpocketLootId)
            {
                if (ids_set.find(lootid) == ids_set.end())
                    LootTemplates_Pickpocketing.ReportNotExistedId(lootid);
                else
                    ids_setUsed.insert(lootid);
            }
        }
    }
    for (uint32 itr : ids_setUsed)
        ids_set.erase(itr);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Pickpocketing.ReportUnusedIds(ids_set);
}

void LoadLootTemplates_Prospecting()
{
    LootIdSet ids_set;
    LootTemplates_Prospecting.LoadAndCollectLootIds(ids_set);

    // remove real entries and check existence loot
    for (uint32 i = 1; i < sItemStorage.GetMaxEntry(); ++i)
    {
        ItemPrototype const* proto = sItemStorage.LookupEntry<ItemPrototype>(i);
        if (!proto)
            continue;

        if (!(proto->Flags & ITEM_FLAG_IS_PROSPECTABLE))
            continue;

        if (ids_set.find(proto->ItemId) != ids_set.end())
            ids_set.erase(proto->ItemId);
        // else -- exist some cases that possible can be prospected but not expected have any result loot
        //    LootTemplates_Prospecting.ReportNotExistedId(proto->ItemId);
    }

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Prospecting.ReportUnusedIds(ids_set);
}

void LoadLootTemplates_Mail()
{
    LootIdSet ids_set;
    LootTemplates_Mail.LoadAndCollectLootIds(ids_set);

    // remove real entries and check existence loot
    for (uint32 i = 1; i < sMailTemplateStore.GetNumRows(); ++i)
        if (sMailTemplateStore.LookupEntry(i))
            if (ids_set.find(i) != ids_set.end())
                ids_set.erase(i);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Mail.ReportUnusedIds(ids_set);
}

void LoadLootTemplates_Skinning()
{
    LootIdSet ids_set, ids_setUsed;
    LootTemplates_Skinning.LoadAndCollectLootIds(ids_set);

    // remove real entries and check existence loot
    for (uint32 i = 1; i < sCreatureStorage.GetMaxEntry(); ++i)
    {
        if (CreatureInfo const* cInfo = sCreatureStorage.LookupEntry<CreatureInfo>(i))
        {
            if (uint32 lootid = cInfo->SkinningLootId)
            {
                if (ids_set.find(lootid) == ids_set.end())
                    LootTemplates_Skinning.ReportNotExistedId(lootid);
                else
                    ids_setUsed.insert(lootid);
            }
        }
    }
    for (uint32 itr : ids_setUsed)
        ids_set.erase(itr);

    // output error for any still listed (not referenced from appropriate table) ids
    LootTemplates_Skinning.ReportUnusedIds(ids_set);
}

void LoadLootTemplates_Reference()
{
    LootIdSet ids_set;
    LootTemplates_Reference.LoadAndCollectLootIds(ids_set);

    // check references and remove used
    LootTemplates_Creature.CheckLootRefs(&ids_set);
    LootTemplates_Fishing.CheckLootRefs(&ids_set);
    LootTemplates_Gameobject.CheckLootRefs(&ids_set);
    LootTemplates_Item.CheckLootRefs(&ids_set);
    LootTemplates_Pickpocketing.CheckLootRefs(&ids_set);
    LootTemplates_Skinning.CheckLootRefs(&ids_set);
    LootTemplates_Disenchant.CheckLootRefs(&ids_set);
    LootTemplates_Prospecting.CheckLootRefs(&ids_set);
    LootTemplates_Mail.CheckLootRefs(&ids_set);
    LootTemplates_Reference.CheckLootRefs(&ids_set);

    // output error for any still listed ids (not referenced from any loot table)
    LootTemplates_Reference.ReportUnusedIds(ids_set);
}