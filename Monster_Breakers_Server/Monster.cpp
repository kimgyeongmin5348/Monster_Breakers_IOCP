#include "Monster.h"
#include "workerthread.h"

// ================================================================
// 스폰 범위 설정 (나중에 상세히 수정할 부분)
// ================================================================
static const float SPAWN_MIN_X = -50.0f;
static const float SPAWN_MAX_X = 50.0f;
static const float SPAWN_Y = 0.0f;  // 지형 높이 (고정)
static const float SPAWN_MIN_Z = -50.0f;
static const float SPAWN_MAX_Z = 50.0f;



// ================================================================
// Monster 생성자
// ================================================================
Monster::Monster(long long id, const XMFLOAT3& spawnPos)
    : m_id(id), m_position(spawnPos), m_spawnPosition(spawnPos)
{
    m_look = { 0.0f, 0.0f, 1.0f }; // 초기 방향 : 앞
    std::cout << "[몬스터] ID=" << m_id
        << " 스폰 위치=(" << spawnPos.x << ", " << spawnPos.y << ", " << spawnPos.z << ")\n";
}

// ================================================================
// 매 틱 Update
// ================================================================
void Monster::Update(float dt, const std::unordered_map<long long, SESSION*>& users)
{
    if (m_isDead) return;

    // 공격 쿨타임 타이머 감소
    if (m_attackCooldownTimer > 0.0f)
        m_attackCooldownTimer -= dt;

    switch (m_state)
    {
    case MonsterState::IDLE:    UpdateIdle(dt, users); break;
    case MonsterState::CHASE:   UpdateChase(dt, users); break;
    case MonsterState::ATTACK:  UpdateAttack(dt, users); break;
    case MonsterState::RETURN:  UpdateReturn(dt);        break;
    }
}

// ================================================================
// 상태별 처리
// ================================================================

void Monster::UpdateIdle(float dt, const std::unordered_map<long long, SESSION*>& users)
{
    float dist = 0.0f;
    SESSION* closest = FindClosestPlayer(users, dist);
    if (!closest) return;

    if (dist <= m_detectRange)
    {
        // 감지 범위 안에 플레이어 들어옴 → 추격 시작
        m_targetPlayerID = closest->_id;
        m_state = MonsterState::CHASE;
        std::cout << "[몬스터] ID=" << m_id << " IDLE → CHASE (타겟=" << m_targetPlayerID << ")\n";
    }
}

void Monster::UpdateChase(float dt, const std::unordered_map<long long, SESSION*>& users)
{
    // 타겟 플레이어가 아직 접속 중인지 확인
    auto it = users.find(m_targetPlayerID);
    if (it == users.end())
    {
        // 타겟이 접속 종료 → 복귀
        m_state = MonsterState::RETURN;
        m_targetPlayerID = -1;
        return;
    }

    SESSION* target = it->second;
    float dist = Distance(m_position, target->_position);

    // 이탈 범위 초과 → 복귀
    if (dist > m_leaveRange)
    {
        m_state = MonsterState::RETURN;
        m_targetPlayerID = -1;
        std::cout << "[몬스터] ID=" << m_id << " CHASE → RETURN (타겟 이탈)\n";
        return;
    }

    // 공격 범위 안으로 들어옴 → 공격 상태
    if (dist <= m_attackRange)
    {
        m_state = MonsterState::ATTACK;
        return;
    }

    // 타겟 방향으로 이동
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
        m_state = MonsterState::RETURN;
        m_targetPlayerID = -1;
        return;
    }

    SESSION* target = it->second;
    float dist = Distance(m_position, target->_position);

    // 타겟이 공격 범위를 벗어남 → 다시 추격
    if (dist > m_attackRange)
    {
        // 이탈 범위도 확인
        if (dist > m_leaveRange)
        {
            m_state = MonsterState::RETURN;
            m_targetPlayerID = -1;
            std::cout << "[몬스터] ID=" << m_id << " ATTACK → RETURN\n";
        }
        else
        {
            m_state = MonsterState::CHASE;
        }
        return;
    }

    // 쿨타임이 끝났으면 공격
    if (m_attackCooldownTimer <= 0.0f)
    {
        target->_hp -= m_attack;
        m_attackCooldownTimer = m_attackCooldown;

        std::cout << "[몬스터] ID=" << m_id
            << " → 플레이어 ID=" << target->_id
            << " 공격! (데미지=" << m_attack
            << " 남은HP=" << target->_hp << ")\n";

        // 플레이어 HP 갱신 패킷 전송
        target->send_player_info_packet();
    }
}

void Monster::UpdateReturn(float dt)
{
    float dist = Distance(m_position, m_spawnPosition);

    // 스폰 위치에 거의 도달하면 IDLE로 전환
    if (dist < 0.5f)
    {
        m_position = m_spawnPosition;
        m_state = MonsterState::IDLE;
        std::cout << "[몬스터] ID=" << m_id << " RETURN → IDLE (복귀 완료)\n";
        return;
    }

    // 스폰 위치 방향으로 이동
    XMFLOAT3 dir = DirectionTo(m_position, m_spawnPosition);
    m_look = dir;
    m_position.x += dir.x * m_moveSpeed * dt;
    m_position.z += dir.z * m_moveSpeed * dt;

    // 복귀 중 위치 브로드캐스트는 g_session 접근 필요 → MonsterManager에서 처리
}

// ================================================================
// 데미지 처리
// ================================================================
void Monster::TakeDamage(int damage, long long attackerID,
    std::unordered_map<long long, SESSION*>& users)
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
    if (len < 0.0001f) return { 0.f, 0.f, 1.f }; // 거의 같은 위치면 기본 방향
    return { dx / len, 0.0f, dz / len };
}

SESSION* Monster::FindClosestPlayer(const std::unordered_map<long long, SESSION*>& users,
    float& outDist) const
{
    SESSION* closest = nullptr;
    outDist = FLT_MAX;

    for (auto& [id, session] : users)
    {
        float d = Distance(m_position, session->_position);
        if (d < outDist)
        {
            outDist = d;
            closest = session;
        }
    }
    return closest;
}

// ================================================================
// 패킷 브로드캐스트
// ================================================================
void Monster::BroadcastMove(const std::unordered_map<long long, SESSION*>& users)
{
    sc_packet_monster_move pkt{};
    pkt.size = sizeof(pkt);
    pkt.type = SC_P_MONSTER_MOVE;
    pkt.monsterID = m_id;
    pkt.position = m_position;
    pkt.rotation = m_look;
    pkt.state = static_cast<int>(m_state);

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

void Monster::BroadcastDeath(long long killerID,
    const std::unordered_map<long long, SESSION*>& users)
{
    std::cout << "[몬스터] ID=" << m_id << " 사망! 처치자=" << killerID
        << " → " << m_goldDrop << " 골드 지급\n";

    // TODO: 골드 지급 패킷 (골드 시스템 구현 후 추가)
    // sc_packet_gold_reward 같은 패킷으로 killerID에게만 전송

    // 사망 브로드캐스트
    // TODO: sc_packet_monster_die 구조체가 protocol.h에 추가되면 사용
     sc_packet_monster_die pkt{};
     pkt.size      = sizeof(pkt);
     pkt.type      = SC_P_MONSTER_DIE;
     pkt.monsterID = m_id;
     for (auto& [id, session] : users)
         session->do_send(&pkt);
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
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = 0; i < count; ++i)
    {
        long long id = m_idCounter++;
        XMFLOAT3  spawnPos = RandomSpawnPosition();
        m_monsters[id] = new Monster(id, spawnPos);
    }
    std::cout << "[몬스터 매니저] " << count << "마리 스폰 완료\n";
}

void MonsterManager::Update(float dt,
    const std::unordered_map<long long, SESSION*>& users)
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

XMFLOAT3 MonsterManager::RandomSpawnPosition()
{
    // 나중에 상세 범위로 교체할 부분
    static std::mt19937 rng(std::random_device{}());
    std::uniform_real_distribution<float> distX(SPAWN_MIN_X, SPAWN_MAX_X);
    std::uniform_real_distribution<float> distZ(SPAWN_MIN_Z, SPAWN_MAX_Z);
    return { distX(rng), SPAWN_Y, distZ(rng) };
}