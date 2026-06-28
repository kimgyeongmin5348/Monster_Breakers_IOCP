#pragma once

#include "common.h"
#include "protocol.h"

class SESSION;

enum class BossAIState : uint8_t {
    IDLE = 0,  
    CHASE = 1,  
    ATTACK = 2, 
    SKILL = 3,
};

enum class BossAttackPattern : uint8_t {
    NORMAL = 0,
    SLAM = 1,
    SWEEP = 2,
};

enum class BossPhase : uint8_t {
    PHASE1 = 0,
    PHASE2 = 1,
};

class BossMonster {
public:
    // 기본
    static constexpr long long  BOSS_ID = 99999;
    static constexpr int        BOSS_MAX_HP = 100;
    static constexpr int        BOSS_ATTACK = 40;
    static constexpr int        BOSS_SKILL_BONUS = 20;

    static constexpr float      RANGE_NORMAL = 3.0f;
    static constexpr float      RANGE_SLAM = 6.0f;
    static constexpr float      RANGE_SWEEP = 5.0f;

    long long                   m_id = BOSS_ID;
    XMFLOAT3                    m_position = { 0.0f, 0.0f, 0.0f };
    XMFLOAT3                    m_look = { 0.0f, 0.0f, 1.0f };
    XMFLOAT3                    m_spawnPos = { 0.0f, 0.0f, 0.0f };

    int                         m_hp = BOSS_MAX_HP;
    int                         m_maxHp = BOSS_MAX_HP;
    int                         m_attack = BOSS_ATTACK;
    float                       m_moveSpeed = 3.0f;
    bool                        m_isDead = false;

    float                       m_detectRange = 8.0f;
    float                       m_attackRange = RANGE_NORMAL;

    // 어그로 관련
    long long                   m_targetPlayerID = -1;
    long long                   m_tauntTargetID = -1;  // 도발한 플레이어
    float                       m_tauntTimer = 0.0f; // 도발 남은 시간

    // 공격 관련
    float                       m_normalAttackCooldown = 2.0f;
    float                       m_normalAttackTimer = 0.0f;
    int                         m_normalAttackCount = 0;
    int                         m_normalAttackUntilSkill = 3; // 지정한 숫자 채우면 스킬 발동

    BossAIState                 m_aiState = BossAIState::IDLE;
    float                       m_patternDuration = 0.0f;
    float                       m_patternTimer = 0.0f;

    BossPhase m_phase = BossPhase::PHASE1;

public:
    BossMonster(const XMFLOAT3& spawnPos);
    ~BossMonster() = default;

    void Update(float dt, const std::unordered_map<long long, SESSION*>& users);
    void TakeDamage(int damage, long long attackerID, std::unordered_map<long long, SESSION*>& users);
    void Taunt(long long tauntPlayerID, float duration);
    bool IsDead() const { return m_isDead; }

private:
    void UpdatePhase();
    void UpdateAI(float dt, const std::unordered_map<long long, SESSION*>& users);

    void PatternNormal(const std::unordered_map<long long, SESSION*>& users);
    void PatternSlam(const std::unordered_map<long long, SESSION*>& users);
    void PatternSweep(const std::unordered_map<long long, SESSION*>& users);

    SESSION* FindTarget(const std::unordered_map<long long, SESSION*>& users) const;
    SESSION* FindClosestPlayer(const std::unordered_map<long long, SESSION*>& users, float range, float& outDist) const;
    float    Distance(const XMFLOAT3& a, const XMFLOAT3& b) const;

    void BroadcastAttackRange(BossAttackPattern pattern, const std::unordered_map<long long, SESSION*>& users);
    void BroadcastBossHP(const std::unordered_map<long long, SESSION*>& users);
    void BroadcastBossDeath(long long killerID, const std::unordered_map<long long, SESSION*>& users);
    void BroadcastBossMove(const std::unordered_map<long long, SESSION*>& users, bool isMoving);
    //float RandomSkillCooldown();
    void ExecuteNormal(const std::unordered_map<long long, SESSION*>& users);
    void ExecuteSlam(const std::unordered_map<long long, SESSION*>& users);
    void ExecuteSweep(const std::unordered_map<long long, SESSION*>& users);
    int  NextSkillThreshold();

    //void BroadcastPattern(uint8_t patternType, float range, const std::unordered_map<long long, SESSION*>& users);
};

BossMonster* SpawnBoss();