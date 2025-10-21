#pragma once
#include "common.h"

class SESSION;

// ���� ���� ����
extern HANDLE g_hIOCP;
extern std::atomic<long long> g_client_counter;
extern std::unordered_map<long long, SESSION*> g_user;
extern std::mutex g_session_mutex;
extern SOCKET g_listen_socket;

enum IO_OP { IO_RECV, IO_SEND, IO_ACCEPT };


class EXP_OVER
{
public:
	EXP_OVER(IO_OP op);


	WSAOVERLAPPED		_over;
	IO_OP				_io_op;
	SOCKET				_accept_socket;
	unsigned char		_buffer[1024];
	WSABUF				_wsabuf[1];


};


class SESSION {
public:
	SOCKET				_c_socket;
	long long			_id;

	EXP_OVER			_recv_over{ IO_RECV };
	unsigned char		_remained;

	XMFLOAT3			_position;
	XMFLOAT3			_look;
	XMFLOAT3			_right;
	uint8_t				_animState;
	short				_hp = 100;
	string				_name;
	atomic<bool>		_is_sending{ false };

public:
	SESSION() = delete;


	SESSION(long long session_id, SOCKET s);


};