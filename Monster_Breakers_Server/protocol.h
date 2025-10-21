#pragma once

#include "common.h"

//#define SET_DATA_FROM_DATABASE
#define SERVER_STRESS_TEST

#define MAX_PACKET_SIZE 1024
#define SERVER_PORT 3000
#define NUM_WORKER_THREADS 4
#define MAX_USER 5000

#define MAX_BUFFER 8192

constexpr char MAX_ID_LENGTH = 20;

// sc ����
constexpr char SC_P_USER_INFO = 1;
constexpr char SC_P_MOVE = 2;
constexpr char SC_P_ENTER = 3;
constexpr char SC_P_LEAVE = 4;

// cs ����
constexpr char CS_P_LOGIN = 5;
constexpr char CS_P_MOVE = 6;



struct sc_packet_user_info {
	unsigned char	size;
	char			type;
	long long		id;
	XMFLOAT3		position;
	XMFLOAT3		look;
	XMFLOAT3		right;
	uint8_t			animState;
	short			hp;
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