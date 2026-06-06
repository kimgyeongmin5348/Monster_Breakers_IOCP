#pragma once
#include "common.h"
#include "protocol.h"

class SESSION;

// 전역 변수 선언
extern HANDLE g_hIOCP;
extern std::atomic<long long> g_client_counter;
extern std::unordered_map<long long, SESSION*> g_session;
extern std::mutex g_session_mutex;
extern SOCKET g_listen_socket;

enum IO_OP { IO_RECV, IO_SEND, IO_ACCEPT };

// 직업별 기본 공격력 조정 및 버프시간 조정
constexpr int BASE_DAMAGE_WARRIOR = 15;
constexpr int BASE_DAMAGE_MAGE = 8;    
constexpr int BASE_DAMAGE_THIEF = 12;
constexpr float ATK_BUFF_DURATION = 8.0f;

class EXP_OVER
{
public:
	EXP_OVER(IO_OP op) : _io_op(op) {
		ZeroMemory(&_over, sizeof(_over));
		_wsabuf[0].buf = reinterpret_cast<CHAR*>(_buffer);
		_wsabuf[0].len = sizeof(_buffer);
	}

	WSAOVERLAPPED		_over;
	IO_OP				_io_op;
	SOCKET				_accept_socket;
	unsigned char		_buffer[1024];
	WSABUF				_wsabuf[1];

};


class SESSION {
public:
	int					_level = 1;
	int					_skillLevel[3] = { 0, 0, 0 };

	SOCKET				_c_socket;
	long long			_id;
	long long			_nickname;
	char				_playerID[MAX_ID_LENGTH];

	EXP_OVER			_recv_over{ IO_RECV };
	unsigned char		_remained;

	bool				_isBlocking = false;
	int					_damage = 10;       // 기본 공격력
	bool				_isAtkBuffed = false;

	int					_baseDamage = 10;
	float				_atkBuffTimer = 0.0f;

	XMFLOAT3			_position;
	XMFLOAT3			_look;
	XMFLOAT3			_right;
	uint8_t				_animState;
	short				_hp = 100;
	int					_gold = 1000;		// 초기 골드 설정
	string				_name;
	uint8_t				_job;
	atomic<bool>		_is_sending{ false };

	bool				_isDead = false;
	float				_respawnTimer = 0.0f;
	XMFLOAT3			_spawnPos = { 0.0f, 0.0f, 0.0f };

	bool				_pendingDelete = false;

public:
	SESSION() = delete;

	SESSION(long long session_id, SOCKET s);

	~SESSION() = default;

	//~SESSION() {
	//	if (_c_socket != INVALID_SOCKET) {
	//		closesocket(_c_socket);
	//		_c_socket = INVALID_SOCKET;
	//	}
	//}

	void do_recv();
	void do_send(void* buff);
	void send_player_info_packet();
	void process_packet(unsigned char* p);
	void Respawn();
	void SetAnimationState(uint8_t state) { _animState = state; }
	uint8_t GetAnimationState() const { return _animState; }

};

void CloseSession(long long id);
void BroadcastToAll(void* pkt, long long exclude_id);
void print_error_message(int s_err);
void do_accept(SOCKET s_socket);
void CloseSession(long long id);
void WorkerThread();

void CheckAndHandleDeath(SESSION* target);