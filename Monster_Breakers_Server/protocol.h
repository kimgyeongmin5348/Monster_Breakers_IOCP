#pragma once

//#define SET_DATA_FROM_DATABASE
#define SERVER_STRESS_TEST

#define MAX_PACKET_SIZE 1024
#define SERVER_PORT 3000
#define NUM_WORKER_THREADS 4
#define MAX_USER 5000

#define BUF_SIZE 1024
#define MAX_BUFFER 8192

constexpr char SC_P_USER_INFO = 1;
constexpr char SC_P_MOVE = 2;
constexpr char SC_P_ENTER = 3;
constexpr char SC_P_LEAVE = 4;
constexpr char CS_P_LOGIN = 5;
constexpr char CS_P_MOVE = 6;
constexpr char SC_P_LOGIN_FAIL = 7;

constexpr char MAX_ID_LENGTH = 20;


constexpr char SC_P_MONSTER_SPAWN = 8;
constexpr char SC_P_MONSTER_MOVE = 9;
constexpr char SC_P_MONSTER_DIE = 10;

// 이건 생각좀 더 해보자
constexpr char SC_P_UPDATE_MONSTER_HP = 27;



constexpr char CS_P_LOADING_DONE = 25;


// =================== 주의!! ========================
// 
// 1. 애니메이션 동기화는 클라에서 애니메이션 완료하면 하기
//		(그전엔 주석처리해둠 : 메모리 크기차이로인한 오류 발생 위험)
// 
// ===================================================

#pragma pack (push, 1)

struct cs_packet_loading_done
{
	unsigned char	size;
	char			type;
};

enum class AnimationState : uint8_t {
	IDLE,         // 0
	WALK,         // 1
	RUN,          // 2 
	JUMP,         // 3
	SWING,        // 4
	CROUCH,       // 5
	CROUCH_WALK   // 6
};

enum PLAYER_JOB {
	JOB_WARRIOR = 0,
	JOB_THIEF = 1,
	JOB_MAGE = 2,
	JOB_MAX
};

struct AnimationBlend
{
	int from = -1;
	int to = -1;
	float duration = 0.5f; // 블렌딩 시간
	float elapsed = 0.0f;
	bool active = false;
};

struct sc_packet_user_info {
	unsigned char	size;
	char			type;
	long long		id;
	XMFLOAT3		position;
	XMFLOAT3		look;
	XMFLOAT3		right;
	uint8_t			animState;
	short			hp;
	uint8_t			job;
};


struct sc_packet_move {
	unsigned char		size;
	char				type;
	long long			id;
	XMFLOAT3			position;
	XMFLOAT3			look;
	XMFLOAT3			right;
	uint8_t				animState;
};


struct sc_packet_enter {
	unsigned char		size;
	char				type;
	long long			id;
	XMFLOAT3			position;
	XMFLOAT3			look;
	XMFLOAT3			right;
	uint8_t				animState;
	short				hp;
	uint8_t				job;

};

struct sc_packet_leave {
	unsigned char		size;
	char				type;
	long long			id;
};


struct cs_packet_login {
	unsigned char		size;
	char				type;
	//XMFLOAT3			position;
	char				name[MAX_ID_LENGTH];
	uint8_t				job;
};


struct sc_packet_login_fail {
	unsigned char		size;
	char				type;
};

struct cs_packet_move {
	unsigned char		size;
	char				type;
	XMFLOAT3			position;
	XMFLOAT3			look;
	XMFLOAT3			right;
	uint8_t				animState;
};



// Monster
enum class MonsterAnimationState : uint8_t
{
	IDLE,
	WALK,
	RUN,
	ATTACK,
	DEATH
};

struct sc_packet_monster_spawn
{
	unsigned char		size;
	char				type;
	long long			monsterID;
	XMFLOAT3			position;
	int					state;
	int					hp;
};

struct sc_packet_monster_move
{
	unsigned char		size;
	char				type;
	long long			monsterID;
	XMFLOAT3			position;
	XMFLOAT3			rotation;
	int					state;
};

struct cs_packet_shovel_damage
{
	unsigned char		size;
	char				type;
	long long			monsterID;
	int					damage;
};

struct sc_packet_update_monster_hp
{
	unsigned char		size;
	char				type;
	long long			monsterID;
	int					hp;
};


//particle
struct cs_packet_flashlight {
	unsigned char		size;
	char				type;
	long long			player_id;
	bool				flashlight_on;
};

struct sc_packet_flashlight {
	unsigned char		size;
	char				type;
	long long			player_id;
	bool				flashlight_on;
};

struct cs_packet_particle_impact {
	unsigned char		size;
	char				type;
	long long			player_id;
	XMFLOAT3			impact_pos;
};

struct sc_packet_particle_impact {
	unsigned char		size;
	char				type;
	long long			player_id;
	XMFLOAT3			impact_pos;
};

#pragma pack (pop)

static_assert(offsetof(sc_packet_user_info, id) == 2, "Packet layout mismatch");
static_assert(offsetof(sc_packet_enter, id) == 2, "Packet layout mismatch");
static_assert(offsetof(sc_packet_move, id) == 2, "Packet layout mismatch");