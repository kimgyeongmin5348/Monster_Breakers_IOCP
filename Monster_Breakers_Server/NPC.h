#pragma once
#include "common.h"
#include "protocol.h"
#include "workerthread.h"

// ================================================================
// 미션 타입 정의
// ================================================================
enum class MissionTargetType
{
    ANY_MONSTER,
    COASTAL_MONSTER  // 해안가 몬스터 (고유ID 10022~10027)
};

struct MissionInfo
{
    int               missionID;
    std::string       description;
    MissionTargetType targetType;
    int               targetCount;
    int               rewardGold;
};

// 플레이어별 진행중인 미션 상태
struct PlayerMissionState
{
    bool   active = false;
    int    missionID = -1;
    int    killCount = 0;
};

class NPCManager
{
public:
    NPCManager();

    // 클라이언트가 NPC와 상호작용했을 때 호출
    void OnNPCInteract(long long playerID, SESSION* session);

    // 몬스터가 죽었을 때 MonsterManager 쪽에서 호출 (killerID = 처치한 플레이어)
    void OnMonsterKilled(long long killerID, long long monsterID, SESSION* session);

private:
    const MissionInfo& PickRandomMission();
    bool IsCoastalMonster(long long monsterID) const;
    void SendMissionInfo(SESSION* session, const MissionInfo& mission);
    void CompleteMission(long long playerID, SESSION* session, const MissionInfo& mission);

private:
    std::vector<MissionInfo> m_missionTable;
    std::unordered_map<long long, PlayerMissionState> m_playerMissions;
    std::mutex m_mutex;

    // 해안가 몬스터 고유ID 범위
    static constexpr long long COASTAL_MONSTER_ID_MIN = 10022;
    static constexpr long long COASTAL_MONSTER_ID_MAX = 10027;
};

extern NPCManager g_npcManager;