#include "Monster.h"
#include "workerthread.h"

// ================================================================
// 몬스터 스폰 좌표 직접 지정
// ================================================================

static const std::vector<XMFLOAT3> SPAWN_POSITIONS = {
    { 100.000f,  0.000f,  -1.000f },
    {  50.000f,  0.000f,  30.000f },
    { -6.000f,  0.000f, 1.000f },

};



// ================================================================
// Monster 생성자
// ================================================================
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

void Monster::Update(float dt, const std::unordered_map<long long, SESSION*>& users)
{
    if (m_isDead) return;

    if (m_attackCooldownTimer > 0.0f)
        m_attackCooldownTimer -= dt;

    switch (m_state)
    {
    case MonsterAIState::IDLE:    UpdateIdle(dt, users); break;
    case MonsterAIState::CHASE:   UpdateChase(dt, users); break;
    case MonsterAIState::ATTACK:  UpdateAttack(dt, users); break;
    case MonsterAIState::RETURN:  UpdateReturn(dt, users); break;
    }
}


void Monster::UpdateIdle(float dt, const std::unordered_map<long long, SESSION*>& users)
{
    float dist = 0.0f;
    SESSION* closest = FindClosestPlayerInRange(users, m_detectRange, dist);
    if (!closest) return;

    m_targetPlayerID = closest->_id;
    m_state = MonsterAIState::CHASE;
    std::cout << "[몬스터] ID=" << m_id << " IDLE → CHASE (타겟=" << m_targetPlayerID << " 거리=" << dist << ")\n";
}

void Monster::UpdateChase(float dt, const std::unordered_map<long long, SESSION*>& users)
{
    auto it = users.find(m_targetPlayerID);
    if (it == users.end())
    {
        m_state = MonsterAIState::RETURN;
        m_targetPlayerID = -1;
        return;
    }

    SESSION* target = it->second;
    float dist = Distance(m_position, target->_position);

    if (dist > m_leaveRange)
    {
        m_state = MonsterAIState::RETURN;
        m_targetPlayerID = -1;
        cout << "[몬스터] ID=" << m_id << " CHASE → RETURN (거리=" << dist << " > " << m_leaveRange << ")\n";
        return;
    }

    if (dist <= m_attackRange)
    {
        m_state = MonsterAIState::ATTACK;
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
    auto it = users.find(m_targetPlayerID);
    if (it == users.end())
    {
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
            m_state = MonsterAIState::RETURN;
            m_targetPlayerID = -1;
            cout << "[몬스터] ID=" << m_id << " ATTACK → RETURN\n";
        }
        else
        {
            m_state = MonsterAIState::CHASE;
        }
        return;
    }

    if (m_attackCooldownTimer <= 0.0f)
    {
        target->_hp -= m_attack;
        m_attackCooldownTimer = m_attackCooldown;

        std::cout << "[몬스터] ID=" << m_id << " → 플레이어 ID=" << target->_id
            << " 공격! (데미지=" << m_attack << " 남은HP=" << target->_hp << ")\n";

        target->send_player_info_packet();
    }
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
    std::cout << "[몬스터] ID=" << m_id
        << " 피격! 데미지=" << damage
        << " 남은HP=" << m_hp << "\n";

    BroadcastHPUpdate(users);

    if (m_hp <= 0)
    {
        m_hp = 0;
        m_isDead = true;
        BroadcastDeath(attackerID, users);
        SendGoldReward(attackerID, users);
    }
}


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

SESSION* Monster::FindClosestPlayerInRange(const std::unordered_map<long long, SESSION*>& users, 
    float range, float& outDist) const
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

    cout << "[골드] ID=" << killerID << " +" << m_goldDrop << "G"
        << " (현재=" << killer->_gold << "G)\n";
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

void MonsterManager::SpawnMonsters(int count)
{
    lock_guard<std::mutex> lock(m_mutex);

    // SPAWN_POSITIONS 배열 범위 초과 방지
    int spawnCount = (std::min)(count, (int)SPAWN_POSITIONS.size());

    for (int i = 0; i < spawnCount; ++i)
    {
        long long id = m_idCounter++;
        m_monsters[id] = new Monster(id, SPAWN_POSITIONS[i]);
    }

    std::cout << "[몬스터 매니저] " << spawnCount << "마리 스폰 완료\n";
}

void MonsterManager::Update(float dt, const std::unordered_map<long long, SESSION*>& users)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& [id, monster] : m_monsters)
    {
        if (!monster->IsDead())
            monster->Update(dt, users);
    }
}

void MonsterManager::OnMonsterHit(long long monsterID, int damage, long long attackerID,
    std::unordered_map<long long, SESSION*>& users)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_monsters.find(monsterID);
    if (it == m_monsters.end())
    {
        std::cout << "[경고] 존재하지 않는 몬스터 ID=" << monsterID << "\n";
        return;
    }
    it->second->TakeDamage(damage, attackerID, users);
}