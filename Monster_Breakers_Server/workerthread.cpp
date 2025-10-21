#include "workerthread.h"
#include "protocol.h"

class SESSION;

HANDLE g_hIOCP;
std::unordered_map<long long, SESSION*> g_user;
std::mutex g_session_mutex;
SOCKET g_listen_socket = INVALID_SOCKET;
std::atomic<long long> g_session_id_counter = 0;

// SESSION ����
SESSION::SESSION(long long session_id, SOCKET s) : _id(session_id), _c_socket(s), _recv_over(IO_RECV)
{
	// ���� �ɼ� �߰� (Keep-Alive ����)
	int opt = 1;
	setsockopt(_c_socket, SOL_SOCKET, SO_KEEPALIVE, (char*)&opt, sizeof(opt));

	// Nagle �˰��� ��Ȱ��ȭ (�ǽð� ��� �ʼ�)
	setsockopt(_c_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));

	{
		std::lock_guard<std::mutex> lock(g_session_mutex);
		g_user[_id] = this;
		std::cout << "[����] ���� �߰� �Ϸ�: ID=" << _id << ", ���� ������ ��: " << g_user.size() << "\n";
	}
	_remained = 0;
	do_recv();
}

void SESSION::do_recv() {
	DWORD recv_flag = 0;
	ZeroMemory(&_recv_over._over, sizeof(_recv_over._over));
	_recv_over._wsabuf[0].buf = reinterpret_cast<CHAR*>(_recv_over._buffer + _remained);
	_recv_over._wsabuf[0].len = sizeof(_recv_over._buffer) - _remained;

	auto ret = WSARecv(_c_socket, _recv_over._wsabuf, 1, NULL, &recv_flag, &_recv_over._over, NULL);
	if (0 != ret) {
		auto err_no = WSAGetLastError();
		if (WSA_IO_PENDING != err_no) {
			std::cout << "[����] " << _id << "�� Ŭ���̾�Ʈ ���� ����. �ڵ�: " << err_no << "\n";
			return;
		}
	}
	/*std::cout << "[����] " << _id << "�� ���� ���� ��� ����\n";*/
}

void SESSION::do_send(void* buff)
{
	if (_c_socket == INVALID_SOCKET) return;

	EXP_OVER* o = new EXP_OVER(IO_SEND);
	const unsigned char packet_size = reinterpret_cast<unsigned char*>(buff)[0];
	memcpy_s(o->_buffer, sizeof(o->_buffer), buff, packet_size);
	o->_wsabuf[0].len = packet_size;

	int ret = WSASend(_c_socket, o->_wsabuf, 1, NULL, 0, &o->_over, NULL);
	if (SOCKET_ERROR == ret) {
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING) {
			std::cout << "[����] ���� ����: " << error << std::endl;
			delete o;
		}
	}
}

void SESSION::send_player_info_packet()
{
	sc_packet_user_info p{};
	p.size = sizeof(p);
	p.type = SC_P_USER_INFO;
	p.id = _id;
	p.position = _position;
	p.look = _look;
	p.right = _right;
	p.animState = _animState;
	p.hp = _hp;
	do_send(&p);
}

void SESSION::process_packet(unsigned char* p)
{
	const unsigned char packet_type = p[1];
	switch (packet_type) {
	case CS_P_LOGIN:
	{
		cs_packet_login* packet = reinterpret_cast<cs_packet_login*>(p);
		_name = packet->name;
		std::cout << "[����] " << _id << "�� Ŭ���̾�Ʈ �α���: " << _name << std::endl;

		send_player_info_packet();
	}

	case CS_P_MOVE:
	{
		cs_packet_move* packet = reinterpret_cast<cs_packet_move*>(p);
		_position = packet->position;
		_look = packet->look;
		_right = packet->right;
	}
		

	default:
		std::cout << "[���] �߸��� ��Ŷ Ÿ��: " << (int)packet_type << "\n";
		return;
	}

}
	


void BroadcastToAll(void* pkt, long long exclude_id = -1)
{
	unsigned char packet_size = reinterpret_cast<unsigned char*>(pkt)[0];
	std::vector<SESSION*> sessions;
	{
		std::lock_guard<std::mutex> lock(g_session_mutex);
		for (auto& [id, session] : g_user) {
			if (session->_c_socket != INVALID_SOCKET && id != exclude_id)
				sessions.push_back(session);
		}
	}

	for (auto session : sessions) {
		auto packet_copy = new char[packet_size]; // ���� ũ�� �Ҵ�
		memcpy(packet_copy, pkt, packet_size);
		session->do_send(packet_copy);
	}
}

void print_error_message(int s_err)
{
	WCHAR* lpMsgBuf;
	FormatMessage(
		FORMAT_MESSAGE_ALLOCATE_BUFFER |
		FORMAT_MESSAGE_FROM_SYSTEM,
		NULL, s_err,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
		(LPTSTR)&lpMsgBuf, 0, NULL);
	std::wcout << lpMsgBuf << std::endl;
	while (true); // ����� ��
	LocalFree(lpMsgBuf);
}

void do_accept(SOCKET s_socket) {
	EXP_OVER* accept_over = new EXP_OVER(IO_ACCEPT);
	SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);

	// ���� �ɼ� ���� (Nagle �˰��� ��Ȱ��ȭ)
	int opt = 1;
	setsockopt(c_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));

	accept_over->_accept_socket = c_socket;

	// AcceptEx ȣ��
	if (!AcceptEx(s_socket, c_socket, accept_over->_buffer, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
		NULL, &accept_over->_over))
	{
		int err = WSAGetLastError();
		if (err != ERROR_IO_PENDING) {
			print_error_message(err);
			delete accept_over;
			closesocket(c_socket);
		}
	}
}

void WorkerThread() {
	while (true) {
		DWORD io_size;
		WSAOVERLAPPED* o;
		ULONG_PTR key;
		BOOL ret = GetQueuedCompletionStatus(g_hIOCP, &io_size, &key, &o, INFINITE);
		EXP_OVER* eo = reinterpret_cast<EXP_OVER*>(o);

		if (FALSE == ret || (0 == io_size && (eo->_io_op == IO_RECV || eo->_io_op == IO_SEND))) {
			if (eo->_io_op == IO_RECV) {
			}
			delete eo;
			continue;
		}

		switch (eo->_io_op)
		{
		case IO_ACCEPT:
		{

			long long new_id = ++g_session_id_counter;
			SOCKET client_socket = eo->_accept_socket;

			// 1. Ŭ���̾�Ʈ �ּ� ���� ����
			SOCKADDR_IN* client_addr = nullptr;
			SOCKADDR_IN* local_addr = nullptr;
			int remote_addr_len = sizeof(SOCKADDR_IN);
			int local_addr_len = sizeof(SOCKADDR_IN);

			GetAcceptExSockaddrs(
				eo->_buffer, 0,
				sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
				(SOCKADDR**)&local_addr, &local_addr_len,
				(SOCKADDR**)&client_addr, &remote_addr_len
			);

			// 2. IP �ּ� ���ڿ� ��ȯ
			char ip_str[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &(client_addr->sin_addr), ip_str, INET_ADDRSTRLEN);
			std::cout << "[����] ���ο� Ŭ���̾�Ʈ ����: IP=" << ip_str
				<< ", ��Ʈ=" << ntohs(client_addr->sin_port)
				<< ", �Ҵ� ID=" << new_id << "\n";

			// 3. IOCP�� ���� ���
			CreateIoCompletionPort(reinterpret_cast<HANDLE>(client_socket), g_hIOCP, new_id, 0);

			// 4. ���� ����
			new SESSION(new_id, client_socket);

			// 5. ���� Accept ��û
			do_accept(g_listen_socket);

			// 6. ���� OVERLAPPED �޸� ����
			delete eo;
			break;

		}

		case IO_SEND:
			delete eo;
			break;

		case IO_RECV:
		{
			// 1. ���ؽ� ������ ���� �˻� (������ ������)
			SESSION* pUser = nullptr;
			{
				std::lock_guard<std::mutex> lock(g_session_mutex);
				auto it = g_user.find(key);
				if (it == g_user.end()) {
					// ������ �̹� ���ŵ� ���
					delete eo;  // EXP_OVER ��ü ����
					continue;
				}
				pUser = it->second;  // ������ ����
			}

			if (FALSE == ret || 0 == io_size) {
				std::cout << "[����] " << key << "�� Ŭ���̾�Ʈ ���� ����\n";
				delete eo;  // EXP_OVER ��ü ����
				continue;
			}

			// 2. ���� �۾� (���� ������ ���¿��� ����)
			SESSION& user = *pUser;  // ������

			unsigned char* p = eo->_buffer;
			int data_size = io_size + user._remained;

			while (p < eo->_buffer + data_size) {
				if (data_size < 2) break; // �ּ� ��Ŷ ũ��(��� 2����Ʈ) Ȯ��
				unsigned char packet_size = p[0];

				// ��Ŷ ũ�� ���� (��� ���� ��ü ũ��)
				if (packet_size < sizeof(unsigned char) ||
					packet_size > MAX_PACKET_SIZE ||
					(p + packet_size) > (eo->_buffer + data_size)) {
					std::cerr << "[����] �߸��� ��Ŷ ũ��: " << (int)packet_size << "\n";
					break;
				}

				user.process_packet(p);
				p += packet_size;
			}

			if (p < eo->_buffer + data_size) {
				user._remained = static_cast<unsigned char>(eo->_buffer + data_size - p);
				memcpy(p, eo->_buffer, user._remained);
			}
			else
				user._remained = 0;
			pUser->do_recv();
			break;
		}
		}
	}
}