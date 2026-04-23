#pragma once
#include "common.h"
#include "protocol.h"

static const int   MONSTER_SPAWN_COUNT = 5; // 서버 시작 시 스폰할 몬스터 수

// 몬스터 AI 상태
enum class MonsterState {
    IDLE,    // 대기 - 원래 자리에서 가만히 있음
    CHASE,   // 추격 - 플레이어 쫒아감
    ATTACK,  // 공격 - 플레이어 공격 범위 내
    RETURN   // 복귀 - 플레이어 이탈 후 원래 위치로 돌아감
};

class SESSION; // 전방 선언

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
    XMFLOAT3    m_position;           // 현재 위치
    XMFLOAT3    m_spawnPosition;      // 스폰 위치 (복귀 목적지)
    XMFLOAT3    m_look;               // 현재 바라보는 방향

    // -------------------------------------------------------
    // AI 설정값
    // -------------------------------------------------------
    float       m_detectRange = 15.0f;  // 플레이어 감지 반경
    float       m_attackRange = 2.5f;   // 공격 가능 반경
    float       m_leaveRange = 25.0f;  // 이 범위 벗어나면 복귀 시작
    float       m_attackCooldown = 1.5f;   // 공격 쿨타임 (초)
    float       m_attackCooldownTimer = 0.0f;  // 현재 쿨타임 타이머

    // -------------------------------------------------------
    // 상태
    // -------------------------------------------------------
    MonsterState            m_state = MonsterState::IDLE;
    long long               m_targetPlayerID = -1;  // 현재 추격 대상 ID

public:
    Monster(long long id, const XMFLOAT3& spawnPos);
    ~Monster() = default;

    // 매 틱 업데이트 (dt: 델타타임 초 단위)
    void Update(float dt, const std::unordered_map<long long, SESSION*>& users);

    // 외부에서 데미지 적용 (공격한 플레이어 ID 필요 - 골드 지급용)
    void TakeDamage(int damage, long long attackerID,
        std::unordered_map<long long, SESSION*>& users);

    bool IsDead() const { return m_isDead; }

private:
    // 상태별 처리
    void UpdateIdle(float dt, const std::unordered_map<long long, SESSION*>& users);
    void UpdateChase(float dt, const std::unordered_map<long long, SESSION*>& users);
    void UpdateAttack(float dt, const std::unordered_map<long long, SESSION*>& users);
    void UpdateReturn(float dt);

    // 유틸
    float       Distance(const XMFLOAT3& a, const XMFLOAT3& b) const;
    XMFLOAT3    DirectionTo(const XMFLOAT3& from, const XMFLOAT3& to) const;
    SESSION* FindClosestPlayer(const std::unordered_map<long long, SESSION*>& users,
        float& outDist) const;

    // 패킷 브로드캐스트
    void BroadcastMove(const std::unordered_map<long long, SESSION*>& users);
    void BroadcastHPUpdate(const std::unordered_map<long long, SESSION*>& users);
    void BroadcastDeath(long long killerID,
        const std::unordered_map<long long, SESSION*>& users);
};

// -------------------------------------------------------
// 몬스터 매니저 - 서버에서 전체 몬스터 관리
// -------------------------------------------------------
class MonsterManager {
public:
    static MonsterManager& GetInstance() {
        static MonsterManager instance;
        return instance;
    }

    void SpawnMonsters(int count);          // 서버 시작 시 몬스터 일괄 스폰
    void Update(float dt,
        const std::unordered_map<long long, SESSION*>& users);  // 매 틱 호출
    void OnMonsterHit(long long monsterID, int damage, long long attackerID,
        std::unordered_map<long long, SESSION*>& users);

    // 전체 몬스터 리스트 (외부 접근용)
    std::unordered_map<long long, Monster*>& GetMonsters() { return m_monsters; }

private:
    MonsterManager() = default;
    ~MonsterManager();

    std::unordered_map<long long, Monster*> m_monsters;
    std::mutex                              m_mutex;
    long long                               m_idCounter = 1000; // 몬스터 ID는 1000번부터

    XMFLOAT3 RandomSpawnPosition(); // 랜덤 스폰 위치 생성
};