#include "Monster.h"
#include "workerthread.h"

// ================================================================
// 비정형 사각형 범위 내 랜덤 좌표 생성
// 이중 삼각형 보간(bilinear) 방식
// ================================================================

static XMFLOAT3 RandomPointInQuad(const XMFLOAT3& A, const XMFLOAT3& B, const XMFLOAT3& C, const XMFLOAT3& D, float y)
{
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_real_distribution<float> dist(0.0f, 1.0f);

    float s = dist(rng);  // 0~1
    float t = dist(rng);  // 0~1

    float x = (1 - s) * (1 - t) * A.x + s * (1 - t) * B.x + s * t * C.x + (1 - s) * t * D.x;
    float z = (1 - s) * (1 - t) * A.z + s * (1 - t) * B.z + s * t * C.z + (1 - s) * t * D.z;

    return { x, y, z };
}

// ================================================================
// 새 구역 랜덤 스폰 좌표 생성 함수
// ================================================================
static XMFLOAT3 GetRandomSpawnInZone()
{

    XMFLOAT3 Z1A = { 20.0f, 0.1f, 78.0f };  // 좌하단
    XMFLOAT3 Z1B = { 6.0f, 0.1f, 140.0f };  // 우하단
    XMFLOAT3 Z1C = { -29.0f, 0.1f, 108.0f }; // 우상단
    XMFLOAT3 Z1D = { -24.0f, 0.1f,  80.0f }; // 좌상단

    return RandomPointInQuad(Z1A, Z1B, Z1C, Z1D, 0.1f);
}

static XMFLOAT3 GetRandomSpawnZone2()
{
    XMFLOAT3 Z2A = { 49.0f, 7.1f, 134.0f };  // 좌하단
    XMFLOAT3 Z2B = { 48.0f, 7.1f, 109.0f };  // 우하단
    XMFLOAT3 Z2C = { 68.0f, 7.1f, 109.0f };  // 우상단
    XMFLOAT3 Z2D = { 67.0f, 7.1f, 136.0f };  // 좌상단
    return RandomPointInQuad(Z2A, Z2B, Z2C, Z2D, 7.5f);
}

static XMFLOAT3 GetRandomSpawnZone3()
{
    XMFLOAT3 Z3A = { 17.2f,  0.0f, -9.0f };  // 좌상단
    XMFLOAT3 Z3B = { 20.9f,  0.0f,   0.8f };  // 우상단
    XMFLOAT3 Z3C = { 28.2f, 0.0f, -1.3f };  // 우하단
    XMFLOAT3 Z3D = { 24.4f, 0.0f, -14.6f };  // 좌하단

    return RandomPointInQuad(Z3A, Z3B, Z3C, Z3D, 0.0f);
}

static XMFLOAT3 GetRandomSpawnZone4()
{
    XMFLOAT3 Z4A = { 32.8f,  0.2f, -9.4f };  // 좌상단
    XMFLOAT3 Z4B = { 37.3f,  0.5f,  10.0f };  // 우상단
    XMFLOAT3 Z4C = { 54.6f, -1.2f,  6.0f };  // 우하단
    XMFLOAT3 Z4D = {50.5f, -1.2f, -18.8f };  // 좌하단

    return RandomPointInQuad(Z4A, Z4B, Z4C, Z4D, -1.0f);
}

Monster::Monster(long long id, const XMFLOAT3& spawnPos)
    : m_id(id), m_position(spawnPos), m_spawnPosition(spawnPos)
{
    m_look = { 0.0f, 0.0f, 1.0f };
    std::cout << "[몬스터] ID=" << m_id << " 스폰 위치=(" << spawnPos.x << ", " << spawnPos.y << ", " << spawnPos.z << ")\n";
}

MonsterState Monster::ToClientAnimState() const
{
    switch (m_state)
    {
    case MonsterAIState::IDLE:    return MonsterState::Idle;
    case MonsterAIState::CHASE:   return MonsterState::Walk;
    case MonsterAIState::ATTACK:  return MonsterState::Attack;
    case MonsterAIState::RETURN:  return MonsterState::Walk;
    default:                      return MonsterState::Idle;
    }
}

// ================================================================
// Update
// ================================================================

void Monster::Update(float dt, const std::unordered_map<long long, SESSION*>& users)
{
    if (m_isDead)
    {
        m_respawnTimer -= dt;
        if (m_respawnTimer <= 0.0f)
            Respawn(users);
        return;
    }

    m_attackCooldownTimer -= dt;

    if (m_isTaunted)
    {
        m_tauntTimer -= dt;
        if (m_tauntTimer <= 0.0f)
        {
            cout << "[도발] ID=" << m_id << " 도발 만료\n";

            m_isTaunted = false;
            m_tauntTargetID = -1;
            m_leaveRange = m_originalLeaveRange;

            // 도발 만료 후 - 현재 상태 유지하되 주변 플레이어 재탐색
            // 1. 현재 타겟이 아직 감지 범위 안에 있으면 -> 그대로 계속 공격
            auto it = users.find(m_targetPlayerID);
            if (it != users.end())
            {
                float dist = Distance(m_position, it->second->_position);
                if (dist <= m_detectRange)
                {
                    // 타겟이 아직 가까이 있음 -> 상태 유지, 아무것도 안 함
                    cout << "[도발만료] ID=" << m_id << " 타겟 아직 범위 내 -> 공격 유지\n";
                    // m_state 건드리지 않음
                }
                else
                {
                    // 타겟이 멀어짐 -> 주변 재탐색
                    float nearDist;
                    SESSION* next = FindClosestPlayerInRange(users, m_detectRange, nearDist);
                    if (next)
                    {
                        m_targetPlayerID = next->_id;
                        m_state = MonsterAIState::CHASE;
                        cout << "[도발만료] ID=" << m_id << " 새 타겟=" << next->_id << "\n";
                    }
                    else
                    {
                        m_state = MonsterAIState::RETURN;
                        m_targetPlayerID = -1;
                        cout << "[도발만료] ID=" << m_id << " 주변 없음 -> 복귀\n";
                    }
                }
            }
            else
            {
                // 타겟 자체가 없음 -> 주변 재탐색
                float nearDist;
                SESSION* next = FindClosestPlayerInRange(users, m_detectRange, nearDist);
                if (next)
                {
                    m_targetPlayerID = next->_id;
                    m_state = MonsterAIState::CHASE;
                    cout << "[도발만료] ID=" << m_id << " 새 타겟=" << next->_id << "\n";
                }
                else
                {
                    m_state = MonsterAIState::RETURN;
                    m_targetPlayerID = -1;
                    cout << "[도발만료] ID=" << m_id << " 주변 없음 -> 복귀\n";
                }
            }
        }
    }

    switch (m_state)
    {
    case MonsterAIState::IDLE:    UpdateIdle(dt, users); break;
    case MonsterAIState::CHASE:   UpdateChase(dt, users); break;
    case MonsterAIState::ATTACK:  UpdateAttack(dt, users); break;
    case MonsterAIState::RETURN:  UpdateReturn(dt, users); break;
    }
}

void Monster::Respawn(const std::unordered_map<long long, SESSION*>& users)
{
    m_hp = m_maxHp;
    m_isDead = false;
    m_state = MonsterAIState::IDLE;
    m_targetPlayerID = -1;
    m_isTaunted = false;
    m_tauntTargetID = -1;
    m_tauntTimer = 0.0f;
    m_attackCooldownTimer = 0.0f;
    m_leaveRange = m_originalLeaveRange;
    m_position = m_spawnPosition;
    m_look = { 0.0f, 0.0f, 1.0f };

    cout << "[리스폰] 몬스터 ID=" << m_id << " 위치=(" << m_position.x << "," << m_position.y << "," << m_position.z << ")" << " HP=" << m_hp << "\n";

    BroadcastRespawn(users);
}

void Monster::UpdateIdle(float dt, const std::unordered_map<long long, SESSION*>& users)
{

    if (m_isTaunted && m_tauntTargetID != -1)
    {
        auto it = users.find(m_tauntTargetID);
        if (it != users.end())
        {
            float dist = Distance(m_position, it->second->_position);
            if (dist <= m_leaveRange)
            {
                m_targetPlayerID = m_tauntTargetID;
                m_state = MonsterAIState::CHASE;
                cout << "[도발] ID=" << m_id << " 도발 타겟 추격 시작 ID=" << m_tauntTargetID << "\n";
                BroadcastMove(users);
                return;
            }
        }
    }

    float dist;
    SESSION* closest = FindClosestPlayerInRange(users, m_detectRange, dist);
    if (!closest) return;

    m_targetPlayerID = closest->_id;
    m_state = MonsterAIState::CHASE;
    cout << "[몬스터] ID=" << m_id << " IDLE → CHASE (타겟=" << m_targetPlayerID << " 거리=" << dist << ")\n";

    BroadcastMove(users);
}

void Monster::UpdateChase(float dt, const std::unordered_map<long long, SESSION*>& users)
{

    if (m_isTaunted && m_tauntTargetID != -1)
        m_targetPlayerID = m_tauntTargetID;

    auto it = users.find(m_targetPlayerID);
    if (it == users.end())
    {

        float dist;
        SESSION* next = FindClosestPlayerInRange(users, m_detectRange, dist);
        if (next) {
            m_targetPlayerID = next->_id;
            cout << "[몬스터] ID=" << m_id << " 타겟소실 -> 새 타겟=" << next->_id << "\n";
            return;
        }

        m_state = MonsterAIState::RETURN;
        m_targetPlayerID = -1;
        return;
    }

    SESSION* target = it->second;
    float dist = Distance(m_position, target->_position);

    if (dist > m_leaveRange)
    {

        float nearDist;
        SESSION* next = FindClosestPlayerInRange(users, m_detectRange, nearDist);
        if (next && next->_id != m_targetPlayerID) {
            m_targetPlayerID = next->_id;
            cout << "[몬스터] ID=" << m_id << " 타겟이탈 -> 새 타겟=" << next->_id << "\n";
            return;
        }

        m_state = MonsterAIState::RETURN;
        m_targetPlayerID = -1;
        cout << "[몬스터] ID=" << m_id << " CHASE → RETURN (거리=" << dist << " > " << m_leaveRange << ")\n";
        return;
    }

    if (dist <= m_attackRange)
    {
        m_state = MonsterAIState::ATTACK;
        BroadcastMove(users);
        return;
    }

    XMFLOAT3 dir = DirectionTo(m_position, target->_position);
    m_look = dir;
    m_position.x += dir.x * m_moveSpeed * dt;
    m_position.z += dir.z * m_moveSpeed * dt;
    BroadcastMove(users);
}

void Monster::UpdateAttack(float dt, const std::unordered_map<long long, SESSION*>& users)
{

    if (m_isTaunted && m_tauntTargetID != -1)
        m_targetPlayerID = m_tauntTargetID;

    auto it = users.find(m_targetPlayerID);
    if (it == users.end())
    {
        float dist;
        SESSION* next = FindClosestPlayerInRange(users, m_detectRange, dist);
        if (next)
        {
            m_targetPlayerID = next->_id;
            m_state = MonsterAIState::CHASE;
            cout << "[몬스터] ID=" << m_id << " 공격타겟소실 -> 새 타겟=" << next->_id << "\n";
            return;
        }
        m_state = MonsterAIState::RETURN;
        m_targetPlayerID = -1;
        return;
    }

    SESSION* target = it->second;
    float dist = Distance(m_position, target->_position);

    if (dist > m_attackRange)
    {
        if (dist > m_leaveRange)
        {
            // leaveRange 벗어나도 주변 재탐색 먼저
            float nearDist;
            SESSION* next = FindClosestPlayerInRange(users, m_detectRange, nearDist);
            if (next && next->_id != m_targetPlayerID)
            {
                m_targetPlayerID = next->_id;
                m_state = MonsterAIState::CHASE;
                cout << "[몬스터] ID=" << m_id << " 공격타겟이탈 -> 새 타겟=" << next->_id << "\n";
                return;
            }
            m_state = MonsterAIState::RETURN;
            m_targetPlayerID = -1;
            cout << "[몬스터] ID=" << m_id << " ATTACK -> RETURN\n";
        }
        else
        {
            m_state = MonsterAIState::CHASE;
        }
        return;
    }

    // 쿨타임 체크 후 데미지 1번만 적용
    if (m_attackCooldownTimer <= 0.0f)
    {
        int finalDamage = target->_isBlocking ? 0 : m_attack;
        target->_hp -= finalDamage;
        m_attackCooldownTimer = m_attackCooldown;  // 3.0f 리셋

        CheckAndHandleDeath(target);
        target->send_player_info_packet();

        cout << "[몬스터공격] 대상ID=" << target->_id << " isBlocking=" << target->_isBlocking << " damage=" << finalDamage << " remainHP=" << target->_hp << "\n";
    }

    BroadcastMove(users);
}

void Monster::UpdateReturn(float dt, const std::unordered_map<long long, SESSION*>& users)
{
    float dist = Distance(m_position, m_spawnPosition);

    if (dist < 0.3f)
    {
        m_position = m_spawnPosition;
        m_state = MonsterAIState::IDLE;
        std::cout << "[몬스터] ID=" << m_id << " RETURN → IDLE (복귀 완료)\n";
        BroadcastMove(users);
        return;
    }

    XMFLOAT3 dir = DirectionTo(m_position, m_spawnPosition);
    m_look = dir;
    m_position.x += dir.x * m_moveSpeed * dt;
    m_position.z += dir.z * m_moveSpeed * dt;
    BroadcastMove(users);

}

void Monster::TakeDamage(int damage, long long attackerID, std::unordered_map<long long, SESSION*>& users)
{
    if (m_isDead) return;

    m_hp -= damage;
    cout << "[몬스터] ID=" << m_id << " 피격! 데미지=" << damage << " 남은HP=" << m_hp << "\n";

    BroadcastHPUpdate(users);

    if (m_hp <= 0)
    {
        m_hp = 0;
        m_isDead = true;
        m_state = MonsterAIState::DEAD;
        m_respawnTimer = RESPAWN_DELAY;
        m_targetPlayerID = -1;
        m_isTaunted = false;

        cout << "[사망] 몬스터 ID=" << m_id << " 사망. " << RESPAWN_DELAY << "초 후 리스폰\n";

        BroadcastDeath(attackerID, users);
        SendGoldReward(attackerID, users);
    }
}



// ================================================================
// 유틸 함수
// ================================================================

float Monster::Distance(const XMFLOAT3& a, const XMFLOAT3& b) const
{
    float dx = a.x - b.x;
    float dz = a.z - b.z;
    return sqrtf(dx * dx + dz * dz); // Y축 무시 (평면 거리)
}

XMFLOAT3 Monster::DirectionTo(const XMFLOAT3& from, const XMFLOAT3& to) const
{
    float dx = to.x - from.x;
    float dz = to.z - from.z;
    float len = sqrtf(dx * dx + dz * dz);
    if (len < 0.0001f) return { 0.f, 0.f, 1.f };
    return { dx / len, 0.0f, dz / len };
}

SESSION* Monster::FindClosestPlayerInRange(const std::unordered_map<long long, SESSION*>& users, float range, float& outDist) const
{
    SESSION* closest = nullptr;
    outDist = FLT_MAX;

    for (auto& [id, session] : users)
    {
        float d = Distance(m_position, session->_position);
        if (d <= range && d < outDist)
        {
            outDist = d;
            closest = session;
        }
    }
    return closest;
}



// ================================================================
// Broadcast
// ================================================================

void Monster::BroadcastMove(const std::unordered_map<long long, SESSION*>& users)
{
    sc_packet_monster_move pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = SC_P_MONSTER_MOVE;
    pkt.monsterID = m_id;
    pkt.position = m_position;
    pkt.rotation = m_look;
    pkt.state = static_cast<int>(ToClientAnimState());

    for (auto& [id, session] : users)
        session->do_send(&pkt);
}

void Monster::BroadcastHPUpdate(const std::unordered_map<long long, SESSION*>& users)
{
    sc_packet_update_monster_hp pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = SC_P_UPDATE_MONSTER_HP;
    pkt.monsterID = m_id;
    pkt.hp = m_hp;

    for (auto& [id, session] : users)
        session->do_send(&pkt);
}

void Monster::BroadcastDeath(long long killerID, const std::unordered_map<long long, SESSION*>& users)
{
    
    cout << "[몬스터] ID=" << m_id << " 사망! 처치자 ID=" << killerID << "\n";
   
     sc_packet_monster_die pkt{};
     pkt.size      = sizeof(pkt);
     pkt.type      = SC_P_MONSTER_DIE;
     pkt.monsterID = m_id;
     pkt.killerID = killerID;

     for (auto& [id, session] : users)
         session->do_send(&pkt);
}

void Monster::BroadcastRespawn(const std::unordered_map<long long, SESSION*>& users)
{
    sc_packet_monster_spawn pkt{};
    pkt.size = sizeof(pkt); pkt.type = SC_P_MONSTER_SPAWN;
    pkt.monsterID = m_id; pkt.position = m_position;
    pkt.hp = m_hp; pkt.state = static_cast<int>(MonsterState::Idle);
    for (auto& [id, session] : users) session->do_send(&pkt);

    cout << "[리스폰] 몬스터 ID=" << m_id << " 리스폰 패킷 전송\n";
}

void Monster::SendGoldReward(long long killerID, const std::unordered_map<long long, SESSION*>& users)
{
    auto it = users.find(killerID);
    if (it == users.end()) return;

    SESSION* killer = it->second;
    killer->_gold += m_goldDrop;

    sc_packet_gold_reward pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = SC_P_GOLD_REWARD;
    pkt.playerID = killerID;
    pkt.amount = m_goldDrop;
    pkt.totalGold = killer->_gold;

    killer->do_send(&pkt);

    cout << "[골드] ID=" << killerID << " +" << m_goldDrop << "G" << " (현재=" << killer->_gold << "G)\n";
}



// ================================================================
// MonsterManager
// ================================================================
MonsterManager::~MonsterManager()
{
    for (auto& [id, monster] : m_monsters)
        delete monster;
    m_monsters.clear();
}

static bool IsFarEnough(const XMFLOAT3& candidate, const std::vector<XMFLOAT3>& placed, float minDist)
{
    for (const auto& p : placed)
    {
        float dx = candidate.x - p.x;
        float dz = candidate.z - p.z;
        if (sqrtf(dx * dx + dz * dz) < minDist)
            return false;
    }
    return true;
}

void MonsterManager::SpawnMonsters(int count)
{
    lock_guard<std::mutex> lock(m_mutex);

    int zone1Count = 12;
    int zone2Count = 9;
    int zone3Count = 3;
    int zone4Count = 3;

    XMFLOAT3 Z1A = { 20.0f, 0.1f, 78.0f };
    XMFLOAT3 Z1B = { 6.0f, 0.1f, 140.0f };
    XMFLOAT3 Z1C = { -29.0f, 0.1f, 108.0f };
    XMFLOAT3 Z1D = { -24.0f, 0.1f,  80.0f };

    XMFLOAT3 Z2A = { 49.0f, 7.1f, 134.0f }; 
    XMFLOAT3 Z2B = { 48.0f, 7.1f, 109.0f }; 
    XMFLOAT3 Z2C = { 68.0f, 7.1f, 109.0f };
    XMFLOAT3 Z2D = { 67.0f, 7.1f, 136.0f }; 

    XMFLOAT3 Z3A = { 17.2f,  0.0f, -9.0f };  
    XMFLOAT3 Z3B = { 20.9f,  0.0f,   0.8f }; 
    XMFLOAT3 Z3C = { 28.2f, 0.0f, -1.3f };  
    XMFLOAT3 Z3D = { 24.4f, 0.0f, -14.6f };  

    XMFLOAT3 Z4A = { 32.8f,  0.2f, -9.4f };
    XMFLOAT3 Z4B = { 37.3f,  0.5f,  10.0f };
    XMFLOAT3 Z4C = { 54.6f, -1.2f,  6.0f };
    XMFLOAT3 Z4D = { 50.5f, -1.2f, -18.8f };

    for (int i = 0; i < zone1Count; ++i)
    {
        XMFLOAT3 pos = GetRandomSpawnInZone();
        long long id = m_idCounter++;
        m_monsters[id] = new Monster(id, pos);
    }

    for (int i = 0; i < zone2Count; ++i)
    {
        XMFLOAT3 pos = GetRandomSpawnZone2();
        long long id = m_idCounter++;
        m_monsters[id] = new Monster(id, pos);
    }

    for (int i = 0; i < zone3Count; ++i)
    {
        XMFLOAT3 pos = GetRandomSpawnZone3();
        long long id = m_idCounter++;
        m_monsters[id] = new Monster(id, pos);
    }

    for (int i = 0; i < zone4Count; ++i)
    {
        XMFLOAT3 pos = GetRandomSpawnZone4();
        long long id = m_idCounter++;
        m_monsters[id] = new Monster(id, pos);
    }

    int total = zone1Count + zone2Count + zone3Count + zone3Count;
    cout << "[몬스터매니저] 총 " << m_monsters.size() << "마리 스폰 완료\n";
}

void MonsterManager::Update(float dt, const std::unordered_map<long long, SESSION*>& users)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, monster] : m_monsters)
        monster->Update(dt, users);

    for (auto& [id, session] : users)
    {
        if (!session->_isAtkBuffed) continue;

        session->_atkBuffTimer -= dt;
        if (session->_atkBuffTimer <= 0.0f)
        {
            session->_damage = session->_baseDamage;
            session->_isAtkBuffed = false;
            session->_atkBuffTimer = 0.0f;

            cout << "[BUFF_ATK 만료] ID=" << id << " 공격력 복구=" << session->_damage << "\n";

            sc_packet_buff_atk expirePkt{};
            expirePkt.size = sizeof(expirePkt);
            expirePkt.type = SC_P_BUFF_ATK;
            expirePkt.playerID = id;
            expirePkt.targetID = id;
            expirePkt.newDamage = session->_damage;
            BroadcastToAll(&expirePkt, -1);
        }
    }
}

void MonsterManager::OnMonsterHit(long long monsterID, int damage, long long attackerID, std::unordered_map<long long, SESSION*>& users)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_monsters.find(monsterID);
    if (it == m_monsters.end())
    {
        cout << "[경고] 존재하지 않는 몬스터 ID=" << monsterID << "\n";
        return;
    }
    it->second->TakeDamage(damage, attackerID, users);
}