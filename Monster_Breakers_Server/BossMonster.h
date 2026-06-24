#pragma once

#include "common.h"
#include "protocol.h"

class SESSION;

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
    static constexpr long long  BOSS_ID = 99999;
    static constexpr int        BOSS_MAX_HP = 50000;
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

    float                       m_detectRange = 30.0f;
    float                       m_attackRange = RANGE_NORMAL;
    long long                   m_targetPlayerID = -1;

    float                       m_normalAttackCooldown = 3.0f;
    float                       m_normalAttackTimer = 0.0f;
    float                       m_skillCooldown = 0.0f;
    float                       m_skillCooldownMax = 12.0f;

    BossPhase m_phase = BossPhase::PHASE1;

public:
    BossMonster(const XMFLOAT3& spawnPos);
    ~BossMonster() = default;

    void Update(float dt, const std::unordered_map<long long, SESSION*>& users);
    void TakeDamage(int damage, long long attackerID, std::unordered_map<long long, SESSION*>& users);
    bool IsDead() const { return m_isDead; }

private:
    void UpdatePhase();
    void UpdateAI(float dt, const std::unordered_map<long long, SESSION*>& users);

    void PatternNormal(const std::unordered_map<long long, SESSION*>& users);
    void PatternSlam(const std::unordered_map<long long, SESSION*>& users);
    void PatternSweep(const std::unordered_map<long long, SESSION*>& users);

    SESSION* FindClosestPlayer(const std::unordered_map<long long, SESSION*>& users, float range, float& outDist) const;
    float    Distance(const XMFLOAT3& a, const XMFLOAT3& b) const;

    void BroadcastAttackRange(BossAttackPattern pattern, const std::unordered_map<long long, SESSION*>& users);
    void BroadcastBossHP(const std::unordered_map<long long, SESSION*>& users);
    void BroadcastBossDeath(long long killerID, const std::unordered_map<long long, SESSION*>& users);
    void BroadcastBossMove(const std::unordered_map<long long, SESSION*>& users);
    float RandomSkillCooldown();
};

BossMonster* SpawnBoss();