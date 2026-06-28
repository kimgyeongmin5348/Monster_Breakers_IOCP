#include "BossMonster.h"    
#include "workerthread.h"  


BossMonster::BossMonster(const XMFLOAT3& spawnPos)
{
    m_position = spawnPos;
    m_spawnPos = spawnPos;
}

float BossMonster::Distance(const XMFLOAT3& a, const XMFLOAT3& b) const
{
    float dx = a.x - b.x;
    float dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz);
}

SESSION* BossMonster::FindClosestPlayer(const std::unordered_map<long long, SESSION*>& users, float range, float& outDist) const
{
    SESSION* closest = nullptr;
    outDist = FLT_MAX;

    for (auto& [id, session] : users) {
        if (!session || session->_isDead) continue;
        float d = Distance(m_position, session->_position);
        if (d <= range && d < outDist) {
            outDist = d;
            closest = session;
        }
    }
    return closest;
}

void BossMonster::Update(float dt, const std::unordered_map<long long, SESSION*>& users)
{
    if (m_isDead) return;

    m_normalAttackTimer += dt;
    if (m_skillCooldown > 0.0f) m_skillCooldown -= dt;

    UpdatePhase();
    UpdateAI(dt, users);

    float dist = FLT_MAX;
    SESSION* target = FindClosestPlayer(users, m_detectRange, dist);

    if (target != nullptr && m_skillCooldown <= 0.0f) {
        if (m_phase == BossPhase::PHASE1) {
            PatternSlam(users);
        }
        else {
            int pick = rand() % 2;
            if (pick == 0) PatternSlam(users);
            else           PatternSweep(users);
        }
        m_skillCooldown = RandomSkillCooldown();
    }

}

void BossMonster::TakeDamage(int damage, long long attackerID,std::unordered_map<long long, SESSION*>& users)
{
    if (m_isDead) return;

    m_hp -= damage;
    if (m_hp < 0) m_hp = 0;

    BroadcastBossHP(users);

    if (m_hp <= 0) {
        m_isDead = true;
        BroadcastBossDeath(attackerID, users);
        cout << "[Boss] 보스 사망 - 공격자 ID=" << attackerID << "\n";
    }
}

void BossMonster::UpdatePhase()
{
    if (m_phase == BossPhase::PHASE1 && m_hp <= BOSS_MAX_HP / 2) {
        m_phase = BossPhase::PHASE2;
        m_attack = BOSS_ATTACK + BOSS_SKILL_BONUS;
        m_skillCooldownMax = 8.0f;
        m_moveSpeed = 4.5f;
        cout << "[Boss] 페이즈2 전환, 공격력=" << m_attack << "\n";
    }
}

void BossMonster::ExecuteNormal(const std::unordered_map<long long, SESSION*>& users)
{
    m_aiState = BossAIState::ATTACK;
    m_patternTimer = 0.0f;
    m_patternDuration = 2.0f;   // 일반공격 애니메이션 길이에 맞게 조정

    BroadcastPattern(0, RANGE_NORMAL, users);   // patternType=0
    PatternNormal(users);
}

void BossMonster::ExecuteSlam(const std::unordered_map<long long, SESSION*>& users)
{
    m_aiState = BossAIState::SKILL;
    m_patternTimer = 0.0f;
    m_patternDuration = 2.5f;
    m_skillCooldown = m_skillCooldownMax;     // 스킬 쿨 리셋

    BroadcastPattern(1, RANGE_SLAM, users);     // patternType=1
    PatternSlam(users);
}

void BossMonster::ExecuteSweep(const std::unordered_map<long long, SESSION*>& users)
{
    m_aiState = BossAIState::SKILL;
    m_patternTimer = 0.0f;
    m_patternDuration = 2.5f;
    m_skillCooldown = m_skillCooldownMax;

    BroadcastPattern(2, RANGE_SWEEP, users);    // patternType=2
    PatternSweep(users);
}

void BossMonster::UpdateAI(float dt, const std::unordered_map<long long, SESSION*>& users)
{
    if (m_isDead) return;

    // 스킬/공격 시전 중이면 타이머만 돌리고 끝나면 복귀
    if (m_aiState == BossAIState::ATTACK || m_aiState == BossAIState::SKILL) {
        m_patternTimer += dt;
        if (m_patternTimer >= m_patternDuration) {
            // 패턴 끝 → 스킬쿨 감소 후 CHASE로 복귀
            if (m_aiState == BossAIState::ATTACK) {
                if (m_skillCooldown > 0.0f)
                    m_skillCooldown -= m_patternDuration;
            }
            m_aiState = BossAIState::CHASE;
            m_patternTimer = 0.0f;
        }
        return; // 패턴 중엔 이동/다른공격 완전 차단
    }

    // 타겟 탐색
    float dist = FLT_MAX;
    SESSION* target = FindClosestPlayer(users, m_detectRange, dist);
    if (!target) {
        m_aiState = BossAIState::IDLE;
        BroadcastBossMove(users, false);
        return;
    }

    float dx = target->_position.x - m_position.x;
    float dz = target->_position.z - m_position.z;
    float len = sqrtf(dx * dx + dz * dz);

    // 공격 범위 밖 -> 추격
    if (len > m_attackRange) {
        m_aiState = BossAIState::CHASE;
        float nx = dx / len, nz = dz / len;
        m_position.x += nx * m_moveSpeed * dt;
        m_position.z += nz * m_moveSpeed * dt;
        m_look = { nx, 0.0f, nz };
        BroadcastBossMove(users, true);   // Walk
        return;
    }

    // 공격 범위 안 -> 정지 후 공격 판단
    BroadcastBossMove(users, false);  // Idle(정지)

    // 스킬쿨 돌았으면 스킬 우선
    if (m_skillCooldown <= 0.0f) {
        int pick = rand() % 2;
        if (pick == 0) ExecuteSlam(users);
        else           ExecuteSweep(users);
        return;
    }

    // 스킬 대기 중 → 일반공격 타이머 체크
    m_normalAttackTimer += dt;
    m_skillCooldown -= dt;

    if (m_normalAttackTimer >= m_normalAttackCooldown) {
        m_normalAttackTimer = 0.0f;
        ExecuteNormal(users);
    }
}

void BossMonster::PatternNormal(const std::unordered_map<long long, SESSION*>& users)
{
    if (m_normalAttackTimer < m_normalAttackCooldown) return;
    m_normalAttackTimer = 0.0f;

    BroadcastAttackRange(BossAttackPattern::NORMAL, users);

    for (auto& [id, session] : users) {
        if (!session || session->_isDead) continue;
        if (Distance(m_position, session->_position) <= RANGE_NORMAL) {
            session->_hp -= static_cast<short>(m_attack);
            session->send_player_info_packet();
            CheckAndHandleDeath(session);
        }
    }
}

// ================================================================
// 패턴 1 - 광역 내려찍기
// ================================================================
void BossMonster::PatternSlam(const std::unordered_map<long long, SESSION*>& users)
{
    BroadcastAttackRange(BossAttackPattern::SLAM, users);

    for (auto& [id, session] : users) {
        if (!session || session->_isDead) continue;
        if (Distance(m_position, session->_position) <= RANGE_SLAM) {
            session->_hp -= static_cast<short>(m_attack * 1.5f);
            session->send_player_info_packet();
            CheckAndHandleDeath(session);
        }
    }
    cout << "[Boss] skill 1 발동!\n";
}

// ================================================================
// 패턴 2 - 전방 휩쓸기
// ================================================================
void BossMonster::PatternSweep(const std::unordered_map<long long, SESSION*>& users)
{
    BroadcastAttackRange(BossAttackPattern::SWEEP, users);

    for (auto& [id, session] : users) {
        if (!session || session->_isDead) continue;

        float dx = session->_position.x - m_position.x;
        float dz = session->_position.z - m_position.z;
        float dist = sqrtf(dx * dx + dz * dz);

        if (dist > RANGE_SWEEP) continue;

        // 전방 120도 판정
        float dot = dx * m_look.x + dz * m_look.z;
        if (dot > 0.5f) {
            session->_hp -= static_cast<short>(m_attack * 1.2f);
            session->send_player_info_packet();
            CheckAndHandleDeath(session);
        }
    }
    cout << "[Boss] skill 2 발동!\n";
}

// ================================================================
// 공격 범위 시각화 패킷
// ================================================================
void BossMonster::BroadcastAttackRange(BossAttackPattern pattern, const std::unordered_map<long long, SESSION*>& users)
{
    sc_packet_boss_pattern pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = SC_P_BOSS_PATTERN;
    pkt.bossID = m_id;
    pkt.patternType = static_cast<uint8_t>(pattern);
    pkt.attackCenter = m_position;
    pkt.sweepAngle = 360.0f;

    switch (pattern) {
    case BossAttackPattern::NORMAL: pkt.attackRange = RANGE_NORMAL; break;
    case BossAttackPattern::SLAM:   pkt.attackRange = RANGE_SLAM;   break;
    case BossAttackPattern::SWEEP:
        pkt.attackRange = RANGE_SWEEP;
        pkt.sweepAngle = 120.0f;
        break;
    }

    for (auto& [id, session] : users)
        session->do_send(&pkt);
}

void BossMonster::BroadcastBossHP(const std::unordered_map<long long, SESSION*>& users)
{
    sc_packet_boss_hp pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = SC_P_BOSS_HP;
    pkt.bossID = m_id;
    pkt.hp = m_hp;
    pkt.maxHp = m_maxHp;

    for (auto& [id, session] : users)
        session->do_send(&pkt);
}

void BossMonster::BroadcastBossDeath(long long killerID, const std::unordered_map<long long, SESSION*>& users)
{
    sc_packet_boss_death pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = SC_P_BOSS_DEATH;
    pkt.bossID = m_id;
    pkt.killerID = killerID;

    for (auto& [id, session] : users)
        session->do_send(&pkt);
}

void BossMonster::BroadcastBossMove(const std::unordered_map<long long, SESSION*>& users, bool isMoving)
{
    sc_packet_boss_move pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = SC_P_BOSS_MOVE;   // ← SPAWN 재활용 말고 전용 패킷 사용
    pkt.bossID = m_id;
    pkt.position = m_position;
    pkt.look = m_look;
    pkt.isMoving = isMoving;

    for (auto& [id, session] : users)
        session->do_send(&pkt);
}

void BossMonster::BroadcastPattern(uint8_t patternType, float range, const std::unordered_map<long long, SESSION*>& users)
{
    sc_packet_boss_pattern pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = SC_P_BOSS_PATTERN;
    pkt.bossID = m_id;
    pkt.patternType = patternType;
    pkt.attackRange = range;
    pkt.attackCenter = m_position;   // 보스 현재 위치를 범위 중심으로

    for (auto& [id, session] : users)
        session->do_send(&pkt);
}

float BossMonster::RandomSkillCooldown()
{
    static std::mt19937 rng{ std::random_device{}() };
    std::uniform_real_distribution<float> dist(
        m_skillCooldownMax * 0.8f,
        m_skillCooldownMax * 1.2f
    );
    return dist(rng);
}

BossMonster* SpawnBoss()
{
    XMFLOAT3 bossSpawnPos = { -16.f, 0.0f, 41.2f };
    BossMonster* boss = new BossMonster(bossSpawnPos);

    sc_packet_boss_spawn pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = SC_P_BOSS_SPAWN;
    pkt.bossID = BossMonster::BOSS_ID;
    pkt.position = bossSpawnPos;
    pkt.hp = BossMonster::BOSS_MAX_HP;
    pkt.maxHp = BossMonster::BOSS_MAX_HP;

    {
        std::lock_guard<std::mutex> lock(g_session_mutex);
        for (auto& [id, session] : g_session)
            session->do_send(&pkt);
    }

    cout << "[Boss] 보스 스폰 완료 ID=" << BossMonster::BOSS_ID << "\n";
    return boss;
}