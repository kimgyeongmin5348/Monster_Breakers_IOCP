#include "NPC.h"


NPCManager g_npcManager;

NPCManager::NPCManager()
{
    m_missionTable.push_back({ 1, "몬스터 10마리를 처치하라.", MissionTargetType::ANY_MONSTER, 10, 5000 });

    m_missionTable.push_back({ 2, "몬스터 5마리를 처치하라.", MissionTargetType::ANY_MONSTER, 5, 2500 });

    m_missionTable.push_back({ 3, "해안가의 몬스터 3마리를 처치하라.", MissionTargetType::COASTAL_MONSTER, 3, 2000 });
}

// ================================================================
// 미션 랜덤 지급
// ================================================================
void NPCManager::OnNPCInteract(long long playerID, SESSION* session)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    const MissionInfo& mission = PickRandomMission();

    PlayerMissionState state;
    state.active = true;
    state.missionID = mission.missionID;
    state.killCount = 0;
    m_playerMissions[playerID] = state;

    SendMissionInfo(session, mission);

    cout << "[NPC] ID=" << playerID << " 미션 지급 -> missionID=" << mission.missionID << " (" << mission.description << ")\n";
}

const MissionInfo& NPCManager::PickRandomMission()
{
    static std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, (int)m_missionTable.size() - 1);
    return m_missionTable[dist(rng)];
}

// ================================================================
// 미션 진행도 체크
// ================================================================
void NPCManager::OnMonsterKilled(long long killerID, long long monsterID, SESSION* session)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    auto it = m_playerMissions.find(killerID);
    if (it == m_playerMissions.end() || !it->second.active) return;

    PlayerMissionState& state = it->second;

    // 현재 진행중인 미션 정보 찾기
    const MissionInfo* mission = nullptr;
    for (auto& m : m_missionTable)
    {
        if (m.missionID == state.missionID) { mission = &m; break; }
    }
    if (!mission) return;

    // 미션 조건에 맞는 몬스터인지 확인
    if (mission->targetType == MissionTargetType::COASTAL_MONSTER)
    {
        if (!IsCoastalMonster(monsterID)) return;  // 해안가 몬스터 아니면 카운트 안 함
    }

    state.killCount++;

    cout << "[NPC] ID=" << killerID << " 미션 진행 " << state.killCount << "/" << mission->targetCount << "\n";

    if (state.killCount >= mission->targetCount)
    {
        CompleteMission(killerID, session, *mission);
    }
}

bool NPCManager::IsCoastalMonster(long long monsterID) const
{
    return monsterID >= COASTAL_MONSTER_ID_MIN && monsterID <= COASTAL_MONSTER_ID_MAX;
}

void NPCManager::CompleteMission(long long playerID, SESSION* session, const MissionInfo& mission)
{
    if (!session) return;

    session->_gold += mission.rewardGold;

    sc_packet_mission_complete pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = SC_P_MISSION_COMPLETE;
    pkt.missionID = mission.missionID;
    pkt.rewardGold = mission.rewardGold;
    pkt.totalGold = session->_gold;
    session->do_send(&pkt);

    // 미션 상태 초기화
    m_playerMissions[playerID].active = false;

    cout << "[NPC] ID=" << playerID << " 미션 완료! missionID=" << mission.missionID << " 보상=" << mission.rewardGold << "G (현재=" << session->_gold << "G)\n";
}

void NPCManager::SendMissionInfo(SESSION* session, const MissionInfo& mission)
{
    if (!session) return;

    sc_packet_npc_mission pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = SC_P_NPC_MISSION;
    pkt.missionID = mission.missionID;
    pkt.targetCount = mission.targetCount;
    pkt.rewardGold = mission.rewardGold;
    strcpy_s(pkt.description, sizeof(pkt.description), mission.description.c_str());

    session->do_send(&pkt);
}