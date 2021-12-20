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

#include "Maps/SpawnGroup.h"

#include "Entities/Creature.h"
#include "Entities/GameObject.h"
#include "Maps/Map.h"
#include "Maps/SpawnGroupDefines.h"
#include "Maps/MapPersistentStateMgr.h"
#include "Globals/ObjectMgr.h"
#include <cassert>

SpawnGroup::SpawnGroup(SpawnGroupEntry const& entry, Map& map, uint32 typeId) : m_entry(entry), m_map(map), m_objectTypeId(typeId), m_enabled(m_entry.EnabledByDefault)
{
}

void SpawnGroup::AddObject(uint32 dbGuid, uint32 entry)
{
    m_objects[dbGuid] = entry;
}

void SpawnGroup::RemoveObject(WorldObject* wo)
{
    m_objects.erase(wo->GetDbGuid());
}

uint32 SpawnGroup::GetGuidEntry(uint32 dbGuid) const
{
    auto itr = m_objects.find(dbGuid);
    if (itr == m_objects.end())
        return 0; // should never happen
    return (*itr).second;
}

void SpawnGroup::Update()
{
    Spawn(false);
}

uint32 SpawnGroup::GetEligibleEntry(std::map<uint32, uint32>& existingEntries, std::map<uint32, uint32>& minEntries)
{
    if (m_entry.RandomEntries.empty())
        return 0;

    if (minEntries.size() > 0)
    {
        auto itr = minEntries.begin();
        std::advance(itr, urand(0, minEntries.size() - 1));
        uint32 entry = (*itr).first;
        --(*itr).second;
        if ((*itr).second == 0)
            minEntries.erase(itr);
        return entry;
    }

    if (m_entry.ExplicitlyChanced.size())
    {
        int32 roll = urand(1, 100);
        for (auto explicitly : m_entry.ExplicitlyChanced)
        {
            if (existingEntries[explicitly->Entry] > 0)
            {
                if (roll < explicitly->Chance)
                    return explicitly->Entry;

                roll -= int32(explicitly->Chance);
            }
        }
    }

    if (m_entry.EquallyChanced.empty())
        return 0;

    auto equallyCopy = m_entry.EquallyChanced;
    std::shuffle(equallyCopy.begin(), equallyCopy.end(), *GetRandomGenerator());

    for (auto equally : equallyCopy)
        if (existingEntries[equally->Entry] > 0)
            return equally->Entry;

    return 0;
}

void SpawnGroup::Spawn(bool force)
{
    if (!m_enabled && !force)
        return;

    if (m_objects.size() >= m_entry.MaxCount || (m_entry.WorldStateId && m_map.GetVariableManager().GetVariable(m_entry.WorldStateId) == 0))
        return;

    std::vector<uint32> eligibleGuids;
    std::map<uint32, uint32> validEntries;
    std::map<uint32, uint32> minEntries;

    for (auto& randomEntry : m_entry.RandomEntries)
    {
        validEntries[randomEntry.Entry] = randomEntry.MaxCount > 0 ? randomEntry.MaxCount : std::numeric_limits<uint32>::max();
        if (randomEntry.MinCount > 0)
            minEntries.emplace(randomEntry.Entry, randomEntry.MinCount);
    }

    for (auto& guid : m_entry.DbGuids)
        eligibleGuids.push_back(guid.DbGuid);

    for (auto& data : m_objects)
    {
        eligibleGuids.erase(std::remove(eligibleGuids.begin(), eligibleGuids.end(), data.first), eligibleGuids.end());
        if (validEntries.size() > 0)
        {
            uint32 curCount = validEntries[data.second];
            validEntries[data.second] = curCount > 0 ? curCount - 1 : 0;
        }
        if (minEntries.size() > 0)
        {
            auto itr = minEntries.find(data.second);
            if (itr != minEntries.end())
            {
                --(*itr).second;
                if ((*itr).second == 0)
                    minEntries.erase(itr);
            }
        }
    }

    time_t now = time(nullptr);
    for (auto itr = eligibleGuids.begin(); itr != eligibleGuids.end();)
    {
        if (m_map.GetPersistentState()->GetObjectRespawnTime(GetObjectTypeId(), *itr) > now)
        {
            if (!force)
            {
                if (m_entry.MaxCount == 1) // rare mob case - prevent respawn until all are off CD
                    return;
                itr = eligibleGuids.erase(itr);
                continue;
            }
            else
                m_map.GetPersistentState()->SaveObjectRespawnTime(GetObjectTypeId(), *itr, now);
        }
        ++itr;
    }

    for (auto itr = eligibleGuids.begin(); itr != eligibleGuids.end();)
    {
        uint32 spawnMask = 0; // safeguarded on db load
        if (GetObjectTypeId() == TYPEID_UNIT)
            spawnMask = sObjectMgr.GetCreatureData(*itr)->spawnMask;
        else
            spawnMask = sObjectMgr.GetGOData(*itr)->spawnMask;
        if (spawnMask && (spawnMask & (1 << m_map.GetDifficulty())) == 0)
        {
            itr = eligibleGuids.erase(itr);
            continue;
        }
        ++itr;
    }

    std::shuffle(eligibleGuids.begin(), eligibleGuids.end(), *GetRandomGenerator());

    for (auto itr = eligibleGuids.begin(); itr != eligibleGuids.end() && !eligibleGuids.empty() && m_objects.size() < m_entry.MaxCount; ++itr)
    {
        uint32 dbGuid = *itr;
        uint32 entry = GetEligibleEntry(validEntries, minEntries);
        float x, y;
        if (GetObjectTypeId() == TYPEID_UNIT)
        {
            auto data = sObjectMgr.GetCreatureData(*itr);
            x = data->posX; y = data->posY;
            m_map.GetPersistentState()->AddCreatureToGrid(dbGuid, data);
        }
        else
        {
            auto data = sObjectMgr.GetGOData(*itr);
            x = data->posX; y = data->posY;
            m_map.GetPersistentState()->AddGameobjectToGrid(dbGuid, data);
        }
        AddObject(dbGuid, entry);
        if (force || m_entry.Active || m_map.IsLoaded(x, y))
        {
            if (GetObjectTypeId() == TYPEID_UNIT)
                WorldObject::SpawnCreature(dbGuid, &m_map, entry);
            else
                WorldObject::SpawnGameObject(dbGuid, &m_map, entry);
        }
        if (entry && validEntries[entry])
            --validEntries[entry];
    }
}

std::string SpawnGroup::to_string() const
{
    std::stringstream result;
    for (auto obj : m_objects)
    {
        std::string cData = "";
        auto creature = m_map.GetCreature(obj.first);
        std::string guidStr = "Not found!";
        if (creature)
            guidStr = creature->GetGuidStr();

        result << "[" << obj.first << ", " << obj.second << "] " << guidStr << "\n";
    }
    return result.str();
}


CreatureGroup::CreatureGroup(SpawnGroupEntry const& entry, Map& map) : SpawnGroup(entry, map, uint32(TYPEID_UNIT))
{
    if (entry.formationEntry)
        m_formationData = std::make_shared<FormationData>(this);
    else
        m_formationData = nullptr;
}

void CreatureGroup::RemoveObject(WorldObject* wo)
{
    SpawnGroup::RemoveObject(wo);
    CreatureData const* data = sObjectMgr.GetCreatureData(wo->GetDbGuid());
    m_map.GetPersistentState()->RemoveCreatureFromGrid(wo->GetDbGuid(), data);
}

void CreatureGroup::TriggerLinkingEvent(uint32 event, Unit* target)
{
    switch (event)
    {
        case CREATURE_GROUP_EVENT_AGGRO:
            if ((m_entry.Flags & CREATURE_GROUP_AGGRO_TOGETHER) == 0)
                return;

            for (auto& data : m_objects)
            {
                uint32 dbGuid = data.first;
                if (Creature* creature = m_map.GetCreature(dbGuid))
                {
                    creature->AddThreat(target);
                    target->AddThreat(creature);
                    target->SetInCombatWith(creature);
                    target->GetCombatManager().TriggerCombatTimer(creature);
                }
            }

            for (uint32 linkedGroup : m_entry.LinkedGroups)
            {
                // ensured on db load that it will be valid fetch
                CreatureGroup* group = static_cast<CreatureGroup*>(m_map.GetSpawnManager().GetSpawnGroup(linkedGroup));
                group->TriggerLinkingEvent(event, target);
            }
            break;
        case CREATURE_GROUP_EVENT_EVADE:
            if ((m_entry.Flags & CREATURE_GROUP_EVADE_TOGETHER) != 0)
            {
                for (auto& data : m_objects)
                {
                    uint32 dbGuid = data.first;
                    if (Creature* creature = m_map.GetCreature(dbGuid))
                        if (!creature->GetCombatManager().IsEvadingHome())
                            creature->AI()->EnterEvadeMode();
                }
            }
            break;
        case CREATURE_GROUP_EVENT_HOME:
        case CREATURE_GROUP_EVENT_RESPAWN:
            if ((m_entry.Flags & CREATURE_GROUP_RESPAWN_TOGETHER) == 0)
                return;

            ClearRespawnTimes();
            break;
    }
}

void CreatureGroup::Update()
{
    SpawnGroup::Update();
}

void CreatureGroup::ClearRespawnTimes()
{
    time_t now = time(nullptr);
    for (auto& data : m_entry.DbGuids)
        m_map.GetPersistentState()->SaveObjectRespawnTime(GetObjectTypeId(), data.DbGuid, now);
}

GameObjectGroup::GameObjectGroup(SpawnGroupEntry const& entry, Map& map) : SpawnGroup(entry, map, uint32(TYPEID_GAMEOBJECT))
{
}

void GameObjectGroup::RemoveObject(WorldObject* wo)
{
    SpawnGroup::RemoveObject(wo);
    GameObjectData const* data = sObjectMgr.GetGOData(wo->GetDbGuid());
    m_map.GetPersistentState()->RemoveGameobjectFromGrid(wo->GetDbGuid(), data);
}


// Formation

#include "Entities/Unit.h"
#include "Database/Database.h"
#include "Policies/Singleton.h"
#include "Entities/Creature.h"

#include "Movement/MoveSplineInit.h"
#include "Movement/MoveSpline.h"

#include "Pools/PoolManager.h"
#include "MotionGenerators/TargetedMovementGenerator.h"
#include "Timer.h"
#include "Maps/MapManager.h"

FormationData::FormationData(CreatureGroup* gData) :
    m_groupData(gData), m_currentFormationShape(gData->GetFormationEntry()->Type),
    m_formationEnabled(false), m_mirrorState(false),
    m_keepCompact(false), m_validFormation(true),
    m_lastWP(0), m_wpPathId(0), m_realMaster(nullptr),
    m_masterMotionType(MovementGeneratorType::STAY_MOTION_TYPE)
{
    for (auto const& sData : gData->GetGroupEntry().DbGuids)
    {
        m_slotsMap.emplace(sData.SlotId, new FormationSlotData(sData.SlotId, sData.DbGuid, gData));

        // save real master guid
        if (sData.SlotId == 0)
            m_realMasterDBGuid = sData.DbGuid;
    }
}

FormationData::~FormationData()
{
    sLog.outDebug("Deleting formation (%u)!!!!!", m_groupData->GetGroupEntry().Id);
}

bool FormationData::SetFollowersMaster()
{
    Unit* master = GetMaster();
    if (!master)
    {
        return false;
    }

    for (auto slotItr : m_slotsMap)
    {
        auto& currentSlot = slotItr.second;

        if (currentSlot == m_masterSlot)
            continue;

        auto follower = currentSlot->GetOwner();

        if (follower && follower->IsAlive())
        {
            bool setMgen = false;
            if (follower->GetMotionMaster()->GetCurrentMovementGeneratorType() != FORMATION_MOTION_TYPE)
                setMgen = true;
            else
            {
                auto mgen = static_cast<FormationMovementGenerator const*>(follower->GetMotionMaster()->GetCurrent());
                if (mgen->GetCurrentTarget() != master)
                    setMgen = true;
            }

            if (setMgen)
            {
                follower->GetMotionMaster()->Clear(false, true);
                follower->GetMotionMaster()->MoveInFormation(currentSlot, true);
                currentSlot->GetRecomputePosition() = true;
            }
        }
    }

    sLog.outString("FormationData::SetFollowersMaste> called for groupId(%u)", m_groupData->GetGroupEntry().Id);

    return false;
}

bool FormationData::SwitchFormation(SpawnGroupFormationType newShape)
{
    if (m_currentFormationShape == newShape)
        return false;

    m_currentFormationShape = newShape;

    FixSlotsPositions();
    return true;
}


// bool FormationData::SetNewMaster(Creature* creature)
// {
//     return TrySetNewMaster(creature);
// }

// remove all creatures from formation data
void FormationData::Disband()
{
    ClearMoveGen();
//     for (auto& slotItr : m_groupData->creatureSlots)
//     {
//         auto& slot = slotItr.second;
// 
//         // creature might be in group but not in formation
//         if (!slot->GetFormationSlotData())
//             continue;
// 
//         slot->GetFormationSlotData().reset();
//     }
// 
//     m_groupData->formationData = nullptr;
}

// remove all movegen (maybe we should remove only move in formation one)
void FormationData::ClearMoveGen()
{
//     for (auto& slotItr : m_groupData->creatureSlots)
//     {
//         auto& slot = slotItr.second;
// 
//         // creature might be in group but not in formation
//         if (!slot->GetFormationSlotData())
//             continue;
// 
//         Unit* slotUnit = slot->GetEntity();
//         if (slotUnit && slotUnit->IsAlive())
//         {
//             if (slotUnit->IsFormationMaster())
//             {
//                 m_lastWP = slotUnit->GetMotionMaster()->getLastReachedWaypoint();
//                 m_wpPathId = slotUnit->GetMotionMaster()->GetPathId();
//             }
//             slotUnit->GetMotionMaster()->Clear(true);
//         }
//     }
}

Unit* FormationData::GetMaster()
{
    if (m_slotsMap.begin() != m_slotsMap.end() && m_slotsMap.begin()->first == 0)
        return m_slotsMap.begin()->second->GetOwner();
    return nullptr;
}

void FormationData::SetMasterMovement()
{
    auto newMaster = m_masterSlot->GetOwner();

    if (!newMaster)
        return;

    newMaster->GetMotionMaster()->Clear(true, true);
    if (m_masterMotionType == WAYPOINT_MOTION_TYPE)
    {
        newMaster->GetMotionMaster()->MoveWaypoint(m_wpPathId, 4, 0, m_groupData->GetFormationEntry()->MovementID);
        newMaster->GetMotionMaster()->SetNextWaypoint(m_lastWP + 1);
        m_wpPathId = 0;
        m_lastWP = 0;
    }
//     else if (m_masterMotionType == MasterMotionType::FORMATION_TYPE_MASTER_LINEAR_WP)
//     {
//         newMaster->GetMotionMaster()->MoveLinearWP(m_wpPathId, 0, 0, 0, m_realMasterGuid, m_lastWP);
//         m_wpPathId = 0;
//         m_lastWP = 0;
//     }
    else if (m_masterMotionType == RANDOM_MOTION_TYPE)
    {
        newMaster->GetMotionMaster()->MoveRandomAroundPoint(m_spawnPos.x, m_spawnPos.y, m_spawnPos.z, m_spawnPos.radius);
    }

    if (!m_realMasterDBGuid)
        m_realMasterDBGuid = m_masterSlot->GetOwner()->GetDbGuid();
}

FormationSlotDataSPtr FormationData::GetFirstEmptySlot()
{
    for (auto slot : m_slotsMap)
    {
        if (!slot.second->GetOwner())
            return slot.second;
    }
    return nullptr;
}

FormationSlotDataSPtr FormationData::GetFirstAliveSlot()
{
    for (auto slot : m_slotsMap)
    {
        if (slot.second->GetOwner() && slot.second->GetOwner()->IsAlive())
            return slot.second;
    }
    return nullptr;
}

bool FormationData::TrySetNewMaster(Unit* masterCandidat /*= nullptr*/)
{
    auto masterSlotItr = m_slotsMap.find(0);
    if (masterSlotItr == m_slotsMap.end())
        return false;

    auto& masterSlot = masterSlotItr->second;

    FormationSlotDataSPtr aliveSlot = nullptr;

    if (masterCandidat && masterCandidat->IsAlive())
    {
        auto candidateSlot = masterCandidat->GetFormationSlot();

        // candidate have to be in this group
        if (candidateSlot && candidateSlot->GetCreatureGroup()->GetGroupId() == m_groupData->GetGroupId())
            aliveSlot = candidateSlot;
    }
    else
    {
        // Get first alive slot
        aliveSlot = GetFirstAliveSlot();
    }

    if (aliveSlot)
    {
        SwitchSlotOwner(m_masterSlot, aliveSlot);
        //Replace(aliveSlot->GetOwner(), m_masterSlot);
//         Unit* oldMaster = nullptr;
//         if (m_masterSlot->GetOwner() && m_masterSlot->GetOwner()->IsAlive())
//             oldMaster = m_masterSlot->GetOwner();
//         Unit* newMasterUnit = aliveSlot->GetOwner();
//         m_masterSlot->SetOwner(newMasterUnit);
//         newMasterUnit->SetFormationSlot(m_masterSlot);
//         
//         aliveSlot->SetOwner(oldMaster);
//         if (oldMaster)
//             oldMaster->SetFormationSlot(aliveSlot);

        FixSlotsPositions();
        SetMasterMovement();
        SetFollowersMaster();
        return true;
    }
    else
    {
        // we can remove this formation from memory
        m_validFormation = false;
    }

    return false;
}

void FormationData::Reset()
{
    if (!m_realMaster || !m_realMaster->IsInWorld())
        return;

    m_mirrorState = false;

    SwitchFormation(m_groupData->GetFormationEntry()->Type);

    FixSlotsPositions();

    // just be sure to fix all position
    //m_needToFixPositions = true;
}

// void FormationData::OnCreate(Unit* entity)
// {
//     if (entity->IsCreature())
//     {
//         auto creature = static_cast<Creature*>(entity);
//         if (creature->IsTemporarySummon())
//         {
//             OnSpawn(entity);
//         }
//     }
// }

void FormationData::OnMasterRemoved()
{
    //m_masterSlot = nullptr;
}

// void FormationData::OnSpawn(Unit* entity)
// {
//     auto freeSlot = m_groupData->GetFirstFreeSlot(entity->GetGUIDLow());
// 
//     MANGOS_ASSERT(freeSlot != nullptr);
// 
//     // respawn of master before FormationData::Update occur
//     if (freeSlot->IsFormationMaster())
//     {
//         TrySetNewMaster(entity);
//         return;
//     }
// 
//     auto master = GetMaster();
//     if (master)
//         entity->Relocate(master->GetPositionX(), master->GetPositionY(), master->GetPositionZ());
// 
//     auto oldSlot = entity->GetGroupSlot();
// 
//     if (freeSlot != oldSlot)
//         Replace(entity, freeSlot);
// 
//     if (m_keepCompact)
//         FixSlotsPositions(true);
// 
//     SetFollowersMaster();
// }

void FormationData::OnDeath(Creature* creature)
{
    auto slot = creature->GetFormationSlot();
    if(!slot)
        return;
    sLog.outString("Deleting creature from formation(%u)", m_groupData->GetGroupEntry().Id);

    bool formationMaster = false;
    if (slot->IsFormationMaster())
    {
       // OnMasterRemoved();
        m_lastWP = creature->GetMotionMaster()->getLastReachedWaypoint();
        m_wpPathId = creature->GetMotionMaster()->GetPathId();

        //m_updateDelay.Reset(0); // Make formation wait 5 sec before choosing another master
        formationMaster = true;
    }
    slot->SetOwner(nullptr);
    creature->SetFormationSlot(nullptr);

    if (formationMaster)
        TrySetNewMaster();
    else if(m_keepCompact)
        FixSlotsPositions();

}

void FormationData::OnDelete(Creature* creature)
{
//     auto slot = creature->GetFormationSlot();
// 
//     if (!slot)
//         return;
// 
//     sLog.outString("Deleting creature from formation(%u)", m_groupData->GetGroupEntry().Id);
//     if (slot->IsFormationMaster())
//     {
//         OnMasterRemoved();
//     }
//     slot->GetOwner() = nullptr;
}

// void FormationData::OnWaypointStart()
// {
//     SetMirrorState(false);
// }
// 
// void FormationData::OnWaypointEnd()
// {
//     SetMirrorState(true);
// }

// get formation slot id for provided dbGuid, return -1 if not found
int32 FormationData::GetDefaultSlotId(uint32 dbGuid)
{
    for (auto const& entry : m_groupData->GetGroupEntry().DbGuids)
        if (entry.DbGuid == dbGuid)
            return entry.SlotId;
    return -1;
}

FormationSlotDataSPtr FormationData::GetDefaultSlot(uint32 dbGuid)
{
    auto slotId = GetDefaultSlotId(dbGuid);
    if (slotId < 0)
        return nullptr;

    return m_slotsMap[slotId];
}

void FormationData::SwitchSlotOwner(FormationSlotDataSPtr slotA, FormationSlotDataSPtr slotB)
{
    Unit* aUnit = slotA->GetOwner();
    Unit* bUnit = slotB->GetOwner();

    slotA->SetOwner(bUnit);
    if (aUnit)
        aUnit->SetFormationSlot(slotB);

    slotB->SetOwner(aUnit);
    if (bUnit)
        bUnit->SetFormationSlot(slotA);
}

bool FormationData::FreeSlot(FormationSlotDataSPtr slot)
{
    if (!slot->GetOwner())
        return true;

    auto newSlot = GetDefaultSlot(slot->GetOwner()->GetDbGuid());

    if (!newSlot || newSlot == slot)
    {
        newSlot = GetFirstEmptySlot();
        if (!newSlot)
        {
            sLog.outError("FormationData::MoveSlotOwner> Unable to find free place in formation groupID: %u for %s",
                m_groupData->GetGroupId(), slot->GetOwner()->GetGuidStr().c_str());
            return false;
        }
    }

    SwitchSlotOwner(slot, newSlot);
    return true;
}

bool FormationData::AddInFormationSlot(Unit* newUnit)
{
    if (!newUnit || !newUnit->IsAlive())
    {
        sLog.outError("FormationData::AddInFormationSlot> Invalid call detected! (unit is nullptr or not alive)");
        return false;
    }

    Unit* oldUnit = nullptr;

    // TODO:: its normal to not have default slot for dynamically added creature/player to the formation we should add one
    auto slot = GetDefaultSlot(newUnit->GetDbGuid());
    if (!slot)
    {
        sLog.outError("FormationData::AddInFormationSlot> Unable to find default slot for %s , is it part of the formation? Aborting...", newUnit->GetGuidStr().c_str());
        return false;
    }

    if (!FreeSlot(slot))
    {
        sLog.outError("FormationData::AddInFormationSlot> Unable to free occupied slot by %s for %s", slot->GetOwner()->GetGuidStr().c_str(), newUnit->GetGuidStr().c_str());
        return false;
    }

    slot->SetOwner(newUnit);
    newUnit->SetFormationSlot(slot);

    sLog.outString("Slot(%u) filled by %s in formation(%u)", slot->GetSlotId(), newUnit->GetGuidStr().c_str(), m_groupData->GetGroupEntry().Id);
    return true;
}

bool FormationData::AddInFormationSlot(Unit* newUnit, FormationSlotDataSPtr newSlot)
{
    if (!newUnit || !newUnit->IsAlive())
    {
        sLog.outError("FormationData::AddInFormationSlot> Invalid call detected! (unit is nullptr or not alive)");
        return false;
    }

    if (!newSlot)
        return AddInFormationSlot(newUnit);

    if (!FreeSlot(newSlot))
    {
        sLog.outError("FormationData::AddInFormationSlot> Unable to free occupied slot by %s for %s", newSlot->GetOwner()->GetGuidStr().c_str(), newUnit->GetGuidStr().c_str());
        return false;
    }

    newSlot->SetOwner(newUnit);
    newUnit->SetFormationSlot(newSlot);

    return true;
}

// replace to either first available slot position or provided one
void FormationData::Replace(Unit* newUnit, FormationSlotDataSPtr newSlot /*= nullptr*/)
{
//     if (!newUnit || !newUnit->IsAlive())
//         return;
// 
//     Unit* oldUnit = nullptr;
//     if (newSlot)
//     {
//         oldUnit = newSlot->GetOwner();
//     }
//     else
//     {
//         newSlot = GetFirstEmptySlot();
//         if (!newSlot)
//         {
//             sLog.outError("FormationData::Replace> Unable to find free place in formation groupID: %u for %s",
//                 m_groupData->GetGroupId(), newUnit->GetGuidStr().c_str());
//             return;
//         }
//     }
// 
//     auto newUnitExistingSlot = newUnit->GetFormationSlot();
//     newSlot->SetOwner(newUnit);
//     newUnit->SetFormationSlot(newSlot);
// 
//     if (newUnitExistingSlot)
//         newUnitExistingSlot->SetOwner(oldUnit);
//     if (oldUnit)
//     {
//         oldUnit->SetFormationSlot(newUnitExistingSlot);
//     }
}

void FormationData::Compact(bool set /*= true*/)
{
    m_keepCompact = set;
    FixSlotsPositions();
}

void FormationData::Add(Creature* creature)
{

}

FormationSlotDataSPtr FormationData::SetFormationSlot(Creature* creature)
{
    if (!creature->IsAlive())
        return nullptr;

    auto const& gEntry = m_groupData->GetGroupEntry();

    if (gEntry.formationEntry == nullptr)
        return nullptr;

    uint32 dbGuid = creature->GetDbGuid();

    // check if existing slot
    if (auto currentSlot = creature->GetFormationSlot())
    {
        // no more work to do
        return std::move(currentSlot);
    }

    // add it in the corresponding slot
    if (!AddInFormationSlot(creature))
        return nullptr;

//     uint32 slotId = -1;
//     for (auto const& entry : gEntry.DbGuids)
//     {
//         if (entry.DbGuid == dbGuid)
//         {
//             slotId = entry.SlotId;
//             break;
//         }
//     }
// 
//     if (slotId < 0)
//         return nullptr;
// 
//     auto& slot = m_slotsMap.at(slotId);
//     if (slot->GetOwner() && slot->GetOwner()->IsAlive())
//     {
//         if (slot->GetOwner()->GetDbGuid() != slot->GetRealOwnerGuid())
//         {
//             // the current owner have to let its place
//             auto freeSlot = GetFirstEmptySlot();
//             if (freeSlot)
//             {
//                 // set old creature to new slot
//                 freeSlot->SetOwner(slot->GetOwner());
//                 freeSlot->GetOwner()->SetFormationSlot(freeSlot);
// 
//                 // set new creature to the slot
//                 slot->SetOwner(creature);
//                 creature->SetFormationSlot(slot);
//             }
//             else
//             {
//                 // no free space remove old creature from formation
//                 Unit* oldUnit = slot->GetOwner();
//                 oldUnit->GetMotionMaster()->Clear(true);
//                 oldUnit->SetFormationSlot(nullptr);
// 
//                 sLog.outError("FormationData::Replace> Unable to find free place in formation groupID: %u for %s",
//                     m_groupData->GetGroupId(), slot->GetOwner()->GetGuidStr().c_str());
// 
//                 // set new creature to the slot
//                 slot->SetOwner(creature);
//                 creature->SetFormationSlot(slot);
//             }
//         }
//         else
//         {
//             // creature in the slot should not be moved
//             //Replace(creature, slot);
//             auto freeSlot = GetFirstEmptySlot();
//             if (freeSlot)
//             {
//                 // assign new creature to free slot
//                 freeSlot->SetOwner(creature);
//                 creature->SetFormationSlot(freeSlot);
//             }
//             else
//             {
//                 // ignore new creature no space for it
//                 sLog.outError("FormationData::Replace> Unable to find free place in formation groupID: %u for %s",
//                     m_groupData->GetGroupId(), creature->GetGuidStr().c_str());
// 
//                 return nullptr;
//             }
//         }
//     }
//     else
//     {
//         slot->SetOwner(creature);
//         creature->SetFormationSlot(slot);
//     }

    //slot->staticSlot = creature->IsTemporarySummon();

    // set the creature as active to avoid some problem
    creature->SetActiveObjectState(true);

    auto slot = creature->GetFormationSlot();
    if (!m_realMaster)
    {
        if (creature->IsTemporarySummon() || (slot->GetSlotId() == 0 && slot->GetRealOwnerGuid() == creature->GetDbGuid()))
        {
            m_formationEnabled = true;
            m_realMaster = creature;
            m_masterSlot = slot;
            creature->GetRespawnCoord(m_spawnPos.x, m_spawnPos.y, m_spawnPos.z, nullptr, &m_spawnPos.radius);

            switch (creature->GetDefaultMovementType())
            {
            case RANDOM_MOTION_TYPE:
                m_masterMotionType = RANDOM_MOTION_TYPE;
                break;
            case WAYPOINT_MOTION_TYPE:
                m_masterMotionType = WAYPOINT_MOTION_TYPE;
                break;
                //case LINEAR_WP_MOTION_TYPE:
                //    m_masterMotionType = MasterMotionType::FORMATION_TYPE_MASTER_LINEAR_WP;
                //    break;
            default:
                sLog.outError("FormationData::FillSlot> Master have not recognized default movement type for formation! Forced to random.");
                m_masterMotionType = RANDOM_MOTION_TYPE;
                break;
            }
        }
    }

    //if (creature->IsAlive())
    //    SetFollowersMaster();

    //FollowMaster(creature);

    if (GetMaster())
    {
        if (slot->GetSlotId() == 0)
            SetMasterMovement();
        FixSlotsPositions();
        SetFollowersMaster();
    }
    return slot;
}

std::string FormationData::to_string() const
{
    std::stringstream result;

    static const std::string FormationType[] = {
        "[0]Random",
        "[1]Single file",
        "[2]Side by side",
        "[3]Like a geese",
        "[4]Fanned out behind",
        "[5]Fanned out in front",
        "[6]Circle the leader"
    };

    std::string fType = FormationType[static_cast<uint32>(m_currentFormationShape)];
    result << "Formation group id: " << m_groupData->GetFormationEntry()->GroupId << "\n";
    result << "Shape: " << fType << "\n";
    result << "Spread: " << m_groupData->GetFormationEntry()->Spread << "\n";
    result << "MovementId: " << m_groupData->GetFormationEntry()->MovementID << "\n";
    result << "Options: " << m_groupData->GetFormationEntry()->Options << "\n";
    result << "Comment: " << m_groupData->GetFormationEntry()->Comment << "\n";


    for (auto slot : m_slotsMap)
    {
        std::string guidStr = "";
        if (slot.second->GetOwner())
            guidStr = slot.second->GetOwner()->GetGuidStr();
        else
            guidStr = "empty slot";

        result << "[" << slot.first << "] " << guidStr << "\n";
    }

    return result.str();
}

void FormationData::FixSlotsPositions()
{
    float defaultDist = m_groupData->GetFormationEntry()->Spread;
    auto& slots = m_slotsMap;
    float totalMembers = 0;
    bool onlyAlive = m_keepCompact;

    if (onlyAlive)
    {
        for (auto& slotItr : slots)
        {
            auto& slot = slotItr.second;

            if (slot->GetOwner() && !slot->GetOwner()->IsAlive())
                continue;

            ++totalMembers;
        }
    }
    else
    {
        totalMembers = m_slotsMap.size();
    }

    if (totalMembers <= 1)
        return;

    // only take account of the followers
    --totalMembers;

    switch (GetFormationType())
    {
        // random formation
        case SPAWN_GROUP_FORMATION_TYPE_RANDOM:
        {
            break;
        }
    
        // single file formation
        case SPAWN_GROUP_FORMATION_TYPE_SINGLE_FILE:
        {
            uint32 membCount = 1;
            for (auto& slotItr : slots)
            {
                auto& slot = slotItr.second;
    
                if (slot->GetOwner() && slot->GetOwner() == GetMaster())
                {
                    slot->SetAngle(0);
                    slot->SetDistance(0);
                    continue;
                }
    
                if (onlyAlive && (!slot->GetOwner() || !slot->GetOwner()->IsAlive()))
                    continue;
    
                slot->SetAngle(M_PI_F);
                slot->SetDistance(defaultDist * membCount);
                slot->GetRecomputePosition() = true;
                ++membCount;
            }
            break;
        }
    
        // side by side formation
        case SPAWN_GROUP_FORMATION_TYPE_SIDE_BY_SIDE:
        {
            uint32 membCount = 1;
            for (auto& slotItr : slots)
            {
                auto& slot = slotItr.second;
    
                if (slot->GetOwner() && slot->GetOwner() == GetMaster())
                {
                    slot->SetAngle(0);
                    slot->SetDistance(0);
                    continue;
                }
    
                if (onlyAlive && (!slot->GetOwner() || !slot->GetOwner()->IsAlive()))
                    continue;
    
                if ((membCount & 1) == 0)
                    slot->SetAngle((M_PI_F / 2.0f) + M_PI_F);
                else
                    slot->SetAngle(M_PI_F / 2.0f);
                slot->SetDistance(defaultDist * (((membCount - 1) / 2) + 1));
                slot->GetRecomputePosition() = true;
                ++membCount;
            }
            break;
        }
    
        // like a geese formation
        case SPAWN_GROUP_FORMATION_TYPE_LIKE_GEESE:
        {
            uint32 membCount = 1;
            for (auto& slotItr : slots)
            {
                auto& slot = slotItr.second;
    
                if (slot->GetOwner() && slot->GetOwner() == GetMaster())
                {
                    slot->SetAngle(0);
                    slot->SetDistance(0);
                    continue;
                }
    
                if (onlyAlive && (!slot->GetOwner() || !slot->GetOwner()->IsAlive()))
                    continue;
    
                if ((membCount & 1) == 0)
                    slot->SetAngle(M_PI_F + (M_PI_F / 4.0f));
                else
                    slot->SetAngle(M_PI_F - (M_PI_F / 3.0f));
                slot->SetDistance(defaultDist* (((membCount - 1) / 2) + 1));
                slot->GetRecomputePosition() = true;
                ++membCount;
            }
            break;
        }
    
        // fanned behind formation
        case SPAWN_GROUP_FORMATION_TYPE_FANNED_OUT_BEHIND:
        {
            uint32 membCount = 1;
            for (auto& slotItr : slots)
            {
                auto& slot = slotItr.second;
    
                if (slot->GetOwner() && slot->GetOwner() == GetMaster())
                {
                    slot->SetAngle(0);
                    slot->SetDistance(0);
                    continue;
                }
    
                if (onlyAlive && (!slot->GetOwner() || !slot->GetOwner()->IsAlive()))
                    continue;
    
                slot->SetAngle((M_PI_F / 2.0f) + (M_PI_F / totalMembers) * (membCount - 1));
                slot->SetDistance(defaultDist);
                slot->GetRecomputePosition() = true;
                ++membCount;
            }
            break;
        }
    
        // fanned in front formation
        case SPAWN_GROUP_FORMATION_TYPE_FANNED_OUT_IN_FRONT:
        {
            uint32 membCount = 1;
            for (auto& slotItr : slots)
            {
                auto& slot = slotItr.second;
    
                if (slot->GetOwner() && slot->GetOwner() == GetMaster())
                {
                    slot->SetAngle(0);
                    slot->SetDistance(0);
                    continue;
                }
    
                if (onlyAlive && (!slot->GetOwner() || !slot->GetOwner()->IsAlive()))
                    continue;
    
                slot->SetAngle(M_PI_F + (M_PI_F / 2.0f) + (M_PI_F / totalMembers) * (membCount - 1));
                if (slot->GetAngle() > M_PI_F * 2.0f)
                    slot->SetAngle(slot->GetAngle() - M_PI_F * 2.0f);
                slot->SetDistance(defaultDist);
                slot->GetRecomputePosition() = true;
                ++membCount;
            }
            break;
        }
    
        // circle formation
        case SPAWN_GROUP_FORMATION_TYPE_CIRCLE_THE_LEADER:
        {
            uint32 membCount = 1;
            for (auto& slotItr : slots)
            {
                auto& slot = slotItr.second;
    
                if (slot->GetOwner() && slot->GetOwner() == GetMaster())
                {
                    slot->SetAngle(0);
                    slot->SetDistance(0);
                    continue;
                }
    
                if (onlyAlive && (!slot->GetOwner() || !slot->GetOwner()->IsAlive()))
                    continue;
    
                slot->SetAngle(((M_PI_F * 2.0f) / totalMembers) * (membCount - 1));
                slot->SetDistance(defaultDist);
                slot->GetRecomputePosition() = true;
                ++membCount;
            }
            break;
        }
        default:
            break;
    }
}

Unit* FormationSlotData::GetMaster()
{
    return GetFormationData()->GetMaster();
}

bool FormationSlotData::IsFormationMaster()
{
    return GetMaster() == m_owner;
}

float FormationSlotData::GetAngle()
{
    return m_angle;
}

float FormationSlotData::GetDistance()
{
    return m_distance;
}
