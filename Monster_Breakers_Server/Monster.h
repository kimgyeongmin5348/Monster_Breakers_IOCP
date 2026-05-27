#pragma once
#include "common.h"
#include "protocol.h"

static const int   MONSTER_SPAWN_COUNT = 20; // 서버 시작 시 스폰할 몬스터 수

enum class MonsterAIState {
    IDLE,
    CHASE,
    ATTACK,
    RETURN
};

class SESSION;

class Monster {
public:
    // -------------------------------------------------------
    // 식별자
    // -------------------------------------------------------

    long long   m_id;           // 몬스터 고유 ID

    // -------------------------------------------------------
    // 스탯
    // -------------------------------------------------------

    int         m_hp = 100;
    int         m_maxHp = 100;
    int         m_attack = 5;
    float       m_moveSpeed = 2.0f;
    int         m_goldDrop = 1000;   // 죽으면 드랍하는 골드
    bool        m_isDead = false;

    // -------------------------------------------------------
    // 위치 정보
    // -------------------------------------------------------

    XMFLOAT3    m_position;
    XMFLOAT3    m_spawnPosition;      // 스폰 위치 (복귀 목적지)
    XMFLOAT3    m_look;

    // -------------------------------------------------------
    // 범위 / 쿨타임
    // -------------------------------------------------------

    float       m_detectRange = 5.0f;
    float       m_attackRange = 2.0f;
    float       m_leaveRange = 5.0f;
    float       m_originalLeaveRange = 5.0f;   // 도발 해제 시 복구용
    float       m_attackCooldown = 3.0f;   // 공격 쿨타임 3초
    float       m_attackCooldownTimer = 0.0f;

    // -------------------------------------------------------
    // 상태
    // -------------------------------------------------------

    MonsterAIState          m_state = MonsterAIState::IDLE;
    long long               m_targetPlayerID = -1;  // 현재 추격 대상 ID

    // -------------------------------------------------------
    // 도발
    // -------------------------------------------------------

    bool        m_isTaunted = false;
    long long   m_tauntTargetID = -1;
    float       m_tauntTimer = 0.0f;
    float       m_tauntDuration = 5.0f;

public:
    Monster(long long id, const XMFLOAT3& spawnPos);
    ~Monster() = default;

    void Update(float dt, const std::unordered_map<long long, SESSION*>& users);

    void TakeDamage(int damage, long long attackerID, std::unordered_map<long long, SESSION*>& users);

    bool IsDead() const { return m_isDead; }

private:
    // 상태별 처리
    void UpdateIdle(float dt, const std::unordered_map<long long, SESSION*>& users);
    void UpdateChase(float dt, const std::unordered_map<long long, SESSION*>& users);
    void UpdateAttack(float dt, const std::unordered_map<long long, SESSION*>& users);
    void UpdateReturn(float dt, const std::unordered_map<long long, SESSION*>& users);

    float       Distance(const XMFLOAT3& a, const XMFLOAT3& b) const;
    XMFLOAT3    DirectionTo(const XMFLOAT3& from, const XMFLOAT3& to) const;
    SESSION* FindClosestPlayerInRange(const std::unordered_map<long long, SESSION*>& users, float range, float& outDist) const;

    void BroadcastMove(const std::unordered_map<long long, SESSION*>& users);
    void BroadcastHPUpdate(const std::unordered_map<long long, SESSION*>& users);
    void BroadcastDeath(long long killerID, const std::unordered_map<long long, SESSION*>& users);
    void SendGoldReward(long long killerID, const std::unordered_map<long long, SESSION*>& users);

    MonsterState ToClientAnimState() const;
};

class MonsterManager {
public:
    static MonsterManager& GetInstance() {
        static MonsterManager instance;
        return instance;
    }

    void SpawnMonsters(int count);
    void Update(float dt, const std::unordered_map<long long, SESSION*>& users);
    void OnMonsterHit(long long monsterID, int damage, long long attackerID, std::unordered_map<long long, SESSION*>& users);

    std::unordered_map<long long, Monster*>& GetMonsters() { return m_monsters; }

private:
    MonsterManager() = default;
    ~MonsterManager();

    std::unordered_map<long long, Monster*> m_monsters;
    std::mutex                              m_mutex;
    long long                               m_idCounter = 10001;
};