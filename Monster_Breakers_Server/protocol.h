#pragma once

#include "common.h"

//------------------- Check List --------------------- 
// 
// <Player>  -> nickname 넣어서 작업해보자.
// 1. 캐릭터 위치, 캐릭터 벡터, 애니메이션 상태, Hp, 캐릭터 Id, 캐릭터 직업
//	 (공격력도 캐릭터의 정보에 들어가야 할까?)
// 
//----------------------------------------------------

//#define SET_DATA_FROM_DATABASE
#define SERVER_STRESS_TEST

#define MAX_PACKET_SIZE 1024
#define SERVER_PORT 3000
#define NUM_WORKER_THREADS 4
#define MAX_USER 5000

#define BUF_SIZE 1024
#define MAX_BUFFER 8192

constexpr char MAX_ID_LENGTH = 20;

// sc 관련
constexpr char SC_P_USER_INFO = 1;
constexpr char SC_P_MOVE = 2;
constexpr char SC_P_ENTER = 3;
constexpr char SC_P_LEAVE = 4;

// cs 관련
constexpr char CS_P_LOGIN = 5;
constexpr char CS_P_MOVE = 6;

constexpr char CS_P_LOADING_DONE = 25;

// =================== 주의!! ========================
// 
// 1. 애니메이션 동기화는 클라에서 애니메이션 완료하면 하기
//		(그전엔 주석처리해둠 : 메모리 크기차이로인한 오류 발생 위험)
// 
// 
// ===================================================


struct cs_packet_loading_done
{
	unsigned char	  size;
	char			  type;
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

struct AnimationBlend
{
	int from = -1;
	int to = -1;
	float duration = 0.5f; // 블렌딩 시간
	float elapsed = 0.0f;
	bool active = false;
};

enum PLAYER_JOB {
	JOB_WARRIOR = 0,
	JOB_THIEF = 1,
	JOB_MAGE = 2,
	JOB_MAX
};

struct sc_packet_user_info {

	unsigned char		size;
	char			    type;
	long long		  	id;
	XMFLOAT3		  	position;
	XMFLOAT3		  	look;
	XMFLOAT3		  	right;
	uint8_t			  	animState;
	short			    hp;
	uint8_t				job;
};


struct sc_packet_move {
	unsigned char		size;
	char		    	type;
	long long			id;
	XMFLOAT3			position;
	XMFLOAT3			look;
	XMFLOAT3			right;
	uint8_t				animState;
};


struct sc_packet_enter {
	unsigned char		size;
	char			    type;
	long long	  		id;
	XMFLOAT3		  	position;
	XMFLOAT3		  	look;
	XMFLOAT3			right;
	uint8_t				animState;
	short				hp;
	uint8_t			    job;
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
	XMFLOAT3		  	look;
	XMFLOAT3		  	right;
	uint8_t			  	animState;
};