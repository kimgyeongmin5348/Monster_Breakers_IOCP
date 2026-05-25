#include "workerthread.h"
#include "Monster.h"
#include "protocol.h"

class SESSION;
class Monster;
class MonsterManager;

HANDLE g_hIOCP;
std::unordered_map<long long, SESSION*> g_session;
std::mutex g_session_mutex;
SOCKET g_listen_socket = INVALID_SOCKET;
//std::atomic<long long> g_session_id_counter = 0;
long long g_session_id_counter = 0;

const char* GetJobName(uint8_t job) {
	switch (static_cast<PLAYER_JOB>(job)) {
	case PLAYER_JOB::JOB_WARRIOR: return "기사";
	case PLAYER_JOB::JOB_THIEF: return "도적";
	case PLAYER_JOB::JOB_MAGE: return "마법사";
	default: return "알수없음";
	}
}

// 스킬 사용자로부터 가장 가까운 다른 플레이어 찾기
SESSION* FindClosestPlayer(long long myID, const XMFLOAT3& myPos)
{
	SESSION* closest = nullptr;
	float minDist = FLT_MAX;

	std::lock_guard<std::mutex> lock(g_session_mutex);
	for (auto& [id, session] : g_session)
	{
		if (id == myID) continue;  // 자신 제외

		float dx = session->_position.x - myPos.x;
		float dz = session->_position.z - myPos.z;
		float dist = sqrtf(dx * dx + dz * dz);

		if (dist < minDist) {
			minDist = dist;
			closest = session;
		}
	}

	cout << "[FindClosest] 스킬사용자 ID=" << myID << " 가장 가까운 플레이어 ID=" << (closest ? closest->_id : -1) << " 거리=" << minDist << "\n";

	return closest;
}

// SESSION 구현
SESSION::SESSION(long long session_id, SOCKET s) : _id(session_id), _c_socket(s), _recv_over(IO_RECV)
{
	// 소켓 옵션 추가 (Keep-Alive 설정)
	int opt = 1;
	setsockopt(_c_socket, SOL_SOCKET, SO_KEEPALIVE, (char*)&opt, sizeof(opt));

	// Nagle 알고리즘 비활성화 (실시간 통신 필수)
	setsockopt(_c_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));

	{
		std::lock_guard<std::mutex> lock(g_session_mutex);
		g_session[_id] = this;
		std::cout << "[서버] 세션 추가 완료: ID=" << _id << ", 현재 접속자 수: " << g_session.size() << "\n";
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
			std::cout << "[오류] " << _id << "번 클라이언트 연결 종료. 코드: " << err_no << "\n";
			return;
		}
	}
	/*std::cout << "[서버] " << _id << "번 소켓 수신 대기 시작\n";*/
}

void SESSION::do_send(void* buff) {
	if (_c_socket == INVALID_SOCKET) return;
	EXP_OVER* over = new EXP_OVER(IO_SEND);
	unsigned char packet_size = reinterpret_cast<unsigned char*>(buff)[0];
	memcpy(over->_buffer, buff, packet_size);
	over->_wsabuf[0].len = packet_size;

	int ret = WSASend(_c_socket, over->_wsabuf, 1, NULL, 0, &over->_over, NULL);
	if (ret == SOCKET_ERROR) {
		int error = WSAGetLastError();
		if (error != WSA_IO_PENDING) {
			std::cout << "[오류] do_send 실패 ID:" << _id << " 에러:" << error << std::endl;
			delete over;
		}
	}
}

void SESSION::Respawn()
{
	_hp = 100;
	_isDead = false;
	_position = _spawnPos;

	cout << "[리스폰] ID=" << _id << " HP=" << _hp << " pos=(" << _position.x << "," << _position.y << "," << _position.z << ")\n";


	send_player_info_packet();

	sc_packet_enter enterPkt{};
	enterPkt.size = sizeof(enterPkt);
	enterPkt.type = SC_P_ENTER;
	enterPkt.id = _id;
	enterPkt.position = _spawnPos;
	enterPkt.look = _look;
	enterPkt.right = _right;
	enterPkt.animState = (uint8_t)AnimationState::IDLE;
	enterPkt.hp = _hp;
	enterPkt.job = _job;
	BroadcastToAll(&enterPkt, _id);

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
	p.job = _job;
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
		_job = packet->job;

		if (_job >= JOB_MAX) {
			cout << "[오류] 잘못된 작업 선택" << endl;
		}

		cout << "[서버] " << _id << "번 클라이언트 로그인: " << _name << "(직업 :" << GetJobName(_job) << ")" << endl;

		switch (_job) {
		case JOB_WARRIOR: _spawnPos = { 3.0f, 0.0f,  20.0f }; break;
		case JOB_MAGE:    _spawnPos = { 3.0f, 0.0f,  21.0f }; break;
		case JOB_THIEF:   _spawnPos = { 3.0f, 0.0f,  22.0f }; break;
		default:          _spawnPos = { 0.0f, 0.0f,  0.0f }; break;
		}
		_position = _spawnPos;

		// 1. 자신의 정보 전송
		send_player_info_packet();

		// 2. 기존 유저 정보 전송
		std::vector<sc_packet_enter> existing_sessions;
		{
			std::lock_guard<std::mutex> lock(g_session_mutex);
			for (auto& [ex_id, ex_session] : g_session) {
				if (ex_id == _id) continue;

				sc_packet_enter pkt;
				pkt.size = sizeof(pkt);
				pkt.type = SC_P_ENTER;
				pkt.id = ex_id;
				pkt.position = ex_session->_position;
				pkt.look = ex_session->_look;
				pkt.right = ex_session->_right;
				pkt.animState = ex_session->GetAnimationState();
				pkt.hp = ex_session->_hp;
				pkt.job = ex_session->_job;
				existing_sessions.push_back(pkt);
			}
		}
		for (auto& pkt : existing_sessions) {
			do_send(&pkt);
		}


		// 3. 신규 유저 정보 브로드캐스트
		sc_packet_enter new_user_pkt;
		new_user_pkt.size = sizeof(new_user_pkt);
		new_user_pkt.type = SC_P_ENTER;
		new_user_pkt.id = _id;
		new_user_pkt.position = _position;
		new_user_pkt.look = _look;
		new_user_pkt.right = _right;
		new_user_pkt.animState = _animState;
		new_user_pkt.hp = _hp;
		new_user_pkt.job = _job;

		std::cout << "Sending ENTER pkt ID: " << new_user_pkt.id << " size: "
			<< static_cast<int>(new_user_pkt.size) << std::endl;


		BroadcastToAll(&new_user_pkt, _id);

		auto& monsters = MonsterManager::GetInstance().GetMonsters();
		for (auto& [monID, mon] : monsters)
		{
			if (mon->IsDead()) continue;

			sc_packet_monster_spawn spawnPkt{};
			spawnPkt.size = sizeof(spawnPkt);
			spawnPkt.type = SC_P_MONSTER_SPAWN;
			spawnPkt.monsterID = monID;
			spawnPkt.position = mon->m_position;
			spawnPkt.hp = mon->m_hp;
			spawnPkt.state = 0;
			do_send(&spawnPkt);  // 본인에게만
		}

		cout << "[로그인] ID=" << _id << " 몬스터 " << monsters.size() << "마리 전송\n";

		break;
	}

	case CS_P_MOVE:
	{
		cs_packet_move* packet = reinterpret_cast<cs_packet_move*>(p);
		_position = packet->position;
		_look = packet->look;
		_right = packet->right;
		_animState = packet->animState;  //애니메이션 완료되면 하기

		//cout << "[위치] ID=" << _id<< " pos=(" << _position.x << ", "	<< _position.y << ", "	<< _position.z << ")\n";

		sc_packet_move mp;
		mp.size = sizeof(mp);
		mp.type = SC_P_MOVE;
		mp.id = _id;
		mp.position = _position;
		mp.look = _look;
		mp.right = _right;
		mp.animState = _animState;

		BroadcastToAll(&mp, _id);

		break;
	}

	case CS_P_LOADING_DONE:
	{
		cs_packet_loading_done* packet = reinterpret_cast<cs_packet_loading_done*>(p);

		std::cout << "[서버] " << _id << "번 클라이언트가 로딩 완료를 알림\n";

		//몬스터 정보 전송 부분 (초기 스폰위치와 상태)

		break;
	}

	case CS_P_USE_GOLD:
	{
		cs_packet_use_gold* packet = reinterpret_cast<cs_packet_use_gold*>(p);
		int cost = packet->amount;


		if (_gold < cost) {
			cout << "[GOLD] ID=" << _id << " 골드 부족 (보유=" << _gold << " 필요=" << cost << ") → 거부\n";


			sc_packet_gold_reward revertPkt{};
			revertPkt.size = sizeof(revertPkt);
			revertPkt.type = SC_P_GOLD_REWARD;
			revertPkt.playerID = _id;
			revertPkt.amount = cost;
			revertPkt.totalGold = _gold;
			do_send(&revertPkt);
			break;
		}

		_gold -= cost;

		cout << "[GOLD] ID=" << _id << " 골드 사용 -" << cost << "G (남은=" << _gold << "G)\n";


		sc_packet_gold_reward syncPkt{};
		syncPkt.size = sizeof(syncPkt);
		syncPkt.type = SC_P_GOLD_REWARD;
		syncPkt.playerID = _id;
		syncPkt.amount = -cost;
		syncPkt.totalGold = _gold;
		do_send(&syncPkt);
		break;
	}

	case CS_P_SKILL:
	{
		cs_packet_skill* packet = reinterpret_cast<cs_packet_skill*>(p);

		std::cout << "[SERVER][SKILL] CS_P_SKILL 수신 | from ID=" << _id
			<< " skillType=" << (int)packet->skillType
			<< " pos=(" << packet->position.x << ", " << packet->position.y << ", " << packet->position.z << ")\n";

		sc_packet_skill broadcast{};
		broadcast.size = sizeof(broadcast);
		broadcast.type = SC_P_SKILL;
		broadcast.playerID = _id;
		broadcast.skillType = packet->skillType;
		broadcast.position = packet->position;
		broadcast.look = packet->look;

		BroadcastToAll(&broadcast, _id); // 본인 제외 전송
		break;
	}

	case CS_P_SHIELD_BLOCK:
	{
		cs_packet_shield_block* packet = reinterpret_cast<cs_packet_shield_block*>(p);
		_isBlocking = packet->isBlocking;

		cout << "[방패막기] ID=" << _id << " isBlocking=" << _isBlocking << "\n";

		sc_packet_shield_block pkt{};
		pkt.size = sizeof(pkt);
		pkt.type = SC_P_SHIELD_BLOCK;
		pkt.playerID = _id;
		pkt.isBlocking = _isBlocking;
		BroadcastToAll(&pkt, _id);
		break;
	}

	case CS_P_SKILL_STRIKE:
	{
		cs_packet_skill_strike* packet = reinterpret_cast<cs_packet_skill_strike*>(p);

		cout << "[강타] ID=" << _id << " pos=(" << packet->position.x << "," << packet->position.y << "," << packet->position.z << ")\n";

		sc_packet_skill_strike pkt{};
		pkt.size = sizeof(pkt);
		pkt.type = SC_P_SKILL_STRIKE;
		pkt.playerID = _id;
		pkt.position = packet->position;
		pkt.look = packet->look;
		BroadcastToAll(&pkt, _id);
		break;
	}

	case CS_P_TAUNT:
	{
		cs_packet_taunt* packet = reinterpret_cast<cs_packet_taunt*>(p);
		float tauntRange = packet->range;
		const float TAUNT_DURATION = 5.0f;

		int affected = 0;
		auto& monsters = MonsterManager::GetInstance().GetMonsters();
		for (auto& [monID, mon] : monsters) {
			if (mon->IsDead()) continue;
			float dx = mon->m_position.x - _position.x;
			float dz = mon->m_position.z - _position.z;
			float dist = sqrtf(dx * dx + dz * dz);
			if (dist <= tauntRange) {
				mon->m_isTaunted = true;
				mon->m_tauntTargetID = _id;
				mon->m_tauntTimer = TAUNT_DURATION;
				mon->m_targetPlayerID = _id;
				mon->m_state = MonsterAIState::CHASE;
				mon->m_originalLeaveRange = mon->m_leaveRange;
				mon->m_leaveRange = tauntRange;
				affected++;
			}
		}

		cout << "[도발] ID=" << _id << " 범위=" << tauntRange << " 영향받은 몬스터=" << affected << "마리\n";

		sc_packet_taunt pkt{};
		pkt.size = sizeof(pkt);
		pkt.type = SC_P_TAUNT;
		pkt.playerID = _id;
		BroadcastToAll(&pkt, _id);
		break;
	}

	case CS_P_BUFF_ATK:
	{

		SESSION* target = FindClosestPlayer(_id, _position);

		if (!target) {
			target = this;
			cout << "[BUFF_ATK] 혼자 → 자기 자신에게 버프\n";
		}

		target->_damage += 20;

		cout << "[BUFF_ATK] 시전자ID=" << _id << " → 대상ID=" << target->_id << " newDamage=" << target->_damage << "\n";


		sc_packet_buff_atk pkt{};
		pkt.size = sizeof(pkt);
		pkt.type = SC_P_BUFF_ATK;
		pkt.playerID = _id;
		pkt.newDamage = _damage;
		BroadcastToAll(&pkt, -1);
		break;
	}

	case CS_P_BUFF_HP:
	{

		const short HP_GAIN = 30;
		const short MAX_HP = 100;
		_hp = min((short)(_hp + HP_GAIN), MAX_HP);

		cout << "[체력버프] ID=" << _id << " newHP=" << _hp << "\n";

		sc_packet_buff_hp pkt{};
		pkt.size = sizeof(pkt);
		pkt.type = SC_P_BUFF_HP;
		pkt.playerID = _id;
		pkt.newHp = _hp;
		BroadcastToAll(&pkt, -1);

		std::vector<SESSION*> targets;
		{
			std::lock_guard<std::mutex> lock(g_session_mutex);
			for (auto& [id, session] : g_session)
			{
				if (id == _id) continue;
				targets.push_back(session);
			}
		}

		for (SESSION* target : targets)
		{
			if (target->_isDead) continue;

			short beforeHp = target->_hp;
			target->_hp = min((short)(target->_hp + HP_GAIN), MAX_HP);

			cout << "[BUFF_HP] 시전자 ID=" << _id << " → 대상 ID=" << target->_id << " HP: " << beforeHp << " → " << target->_hp << "\n";

			sc_packet_buff_hp pkt{};
			pkt.size = sizeof(pkt);
			pkt.type = SC_P_BUFF_HP;
			pkt.playerID = target->_id;
			pkt.newHp = target->_hp;
			BroadcastToAll(&pkt, -1);
		}

		cout << "[BUFF_HP] 총 " << targets.size() + 1 << "명 회복 완료\n";

		break;
	}

	case CS_P_WEAPON_POS:
	{
		cs_packet_weapon_pos* packet = reinterpret_cast<cs_packet_weapon_pos*>(p);

		cout << "[도끼위치] ID=" << _id << " pos=(" << packet->weaponPosition.x << "," << packet->weaponPosition.y << "," << packet->weaponPosition.z << ")\n";

		sc_packet_weapon_pos pkt{};
		pkt.size = sizeof(pkt);
		pkt.type = SC_P_WEAPON_POS;
		pkt.playerID = _id;
		pkt.weaponPosition = packet->weaponPosition;
		pkt.weaponRotation = packet->weaponRotation;
		BroadcastToAll(&pkt, _id);
		break;
	}

	case CS_P_HIT_DAMAGE:
	{
		cs_packet_hit_damage* packet = reinterpret_cast<cs_packet_hit_damage*>(p);

		cout << "[서버] ID=" << _id << "번 플레이어 → 몬스터 ID=" << packet->monsterID << " 공격 요청. 데미지=" << packet->damage << "\n";

		// 1. 데미지 값 검증 (비정상 값 방지)
		if (packet->damage <= 0 || packet->damage > 9999)
		{
			cout << "[경고] ID=" << _id << " 비정상 데미지=" << packet->damage << " → 무시\n";
			break;
		}

		// 2. 몬스터 존재 여부 확인
		auto& monsterMap = MonsterManager::GetInstance().GetMonsters();
		auto monIt = monsterMap.find(packet->monsterID);
		if (monIt == monsterMap.end())
		{
			cout << "[경고] 존재하지 않는 몬스터 ID=" << packet->monsterID << " → 무시\n";
			break;
		}

		Monster* pMonster = monIt->second;

		// 3. 이미 죽은 몬스터면 무시
		if (pMonster->IsDead())
		{
			std::cout << "[경고] 이미 죽은 몬스터 ID=" << packet->monsterID << " → 무시\n";
			break;
		}

		// 4. 서버에서 거리 검증 (근접 공격 유효 범위)
		const float MELEE_RANGE = 2.5f;
		float dx = _position.x - pMonster->m_position.x;
		float dz = _position.z - pMonster->m_position.z;
		float dist = sqrtf(dx * dx + dz * dz);

		if (dist > MELEE_RANGE)
		{
			std::cout << "[경고] ID=" << _id
				<< " 범위 초과 공격 무시 (거리=" << dist
				<< " > " << MELEE_RANGE << ")\n";
			break;
		}

		std::cout << "[서버] 거리 검증 통과 (거리=" << dist << ")"
			<< " → 몬스터 ID=" << packet->monsterID
			<< " 데미지=" << packet->damage << " 처리\n";

		// 5. 세션 스냅샷 복사 후 데미지 처리
		std::unordered_map<long long, SESSION*> snapshot;
		{
			std::lock_guard<std::mutex> lock(g_session_mutex);
			snapshot = g_session;
		}

		MonsterManager::GetInstance().OnMonsterHit(
			packet->monsterID,
			packet->damage,
			_id,        // 공격한 플레이어 ID (골드 지급용)
			snapshot
		);
		break;
	}
	default:
		std::cout << "[경고] 잘못된 패킷 타입: " << (int)packet_type << "\n";
		return;
	}

}

void CheckAndHandleDeath(SESSION* target)
{
	if (target->_hp > 0 || target->_isDead) return;

	target->_hp = 0;
	target->_isDead = true;

	int beforeGold = target->_gold;
	target->_gold = target->_gold / 2;

	cout << "[사망] ID=" << target->_id << " 골드 패널티: " << beforeGold << " → " << target->_gold << "G\n";

	sc_packet_gold_reward goldPkt{};
	goldPkt.size = sizeof(goldPkt);
	goldPkt.type = SC_P_GOLD_REWARD;
	goldPkt.playerID = target->_id;
	goldPkt.amount = -(beforeGold - target->_gold);
	goldPkt.totalGold = target->_gold;
	target->do_send(&goldPkt);

	target->_respawnTimer = 0.01f;

	cout << "[사망] ID=" << target->_id << " 사망 처리 → 3초 후 리스폰\n";

}

void BroadcastToAll(void* pkt, long long exclude_id = -1) {
	unsigned char packet_size = reinterpret_cast<unsigned char*>(pkt)[0];
	std::vector<SESSION*> sessions;
	{
		std::lock_guard<std::mutex> lock(g_session_mutex);
		for (auto& pair : g_session) {
			if (pair.second->_c_socket != INVALID_SOCKET && pair.first != exclude_id) {
				sessions.push_back(pair.second);
			}
		}
	}
	for (auto* session : sessions) {
		session->do_send(pkt);  // do_send 호출로 통일
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
	while (true); // 디버깅 용
	LocalFree(lpMsgBuf);
}

void do_accept(SOCKET s_socket) {
	EXP_OVER* accept_over = new EXP_OVER(IO_ACCEPT);
	SOCKET c_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);

	// 소켓 옵션 설정 (Nagle 알고리즘 비활성화)
	int opt = 1;
	setsockopt(c_socket, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt));

	accept_over->_accept_socket = c_socket;

	// AcceptEx 호출
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

			// 1. 클라이언트 주소 정보 추출
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

			// 2. IP 주소 문자열 변환
			char ip_str[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &(client_addr->sin_addr), ip_str, INET_ADDRSTRLEN);
			std::cout << "[서버] 새로운 클라이언트 접속: IP=" << ip_str
				<< ", 포트=" << ntohs(client_addr->sin_port)
				<< ", 할당 ID=" << new_id << "\n";

			// 3. IOCP에 소켓 등록
			CreateIoCompletionPort(reinterpret_cast<HANDLE>(client_socket), g_hIOCP, new_id, 0);

			// 4. 세션 생성
			new SESSION(new_id, client_socket);

			// 5. 다음 Accept 요청
			do_accept(g_listen_socket);

			// 6. 현재 OVERLAPPED 메모리 해제
			delete eo;
			break;

		}

		case IO_SEND:
			delete eo;
			break;

		case IO_RECV:
		{
			// 1. 뮤텍스 락으로 세션 검색 (스레드 세이프)
			SESSION* pUser = nullptr;
			{
				std::lock_guard<std::mutex> lock(g_session_mutex);
				auto it = g_session.find(key);
				if (it == g_session.end()) {
					// 세션이 이미 제거된 경우
					delete eo;  // EXP_OVER 객체 정리
					continue;
				}
				pUser = it->second;  // 포인터 추출
			}

			if (FALSE == ret || 0 == io_size) {
				std::cout << "[서버] " << key << "번 클라이언트 연결 종료\n";
				delete eo;  // EXP_OVER 객체 정리
				continue;
			}

			// 2. 세션 작업 (락이 해제된 상태에서 진행)
			SESSION& user = *pUser;  // 역참조

			unsigned char* p = eo->_buffer;
			int data_size = io_size + user._remained;

			while (p < eo->_buffer + data_size) {
				if (data_size < 2) break; // 최소 패킷 크기(헤더 2바이트) 확인
				unsigned char packet_size = p[0];

				// 패킷 크기 검증 (헤더 포함 전체 크기)
				if (packet_size < sizeof(unsigned char) ||
					packet_size > MAX_PACKET_SIZE ||
					(p + packet_size) > (eo->_buffer + data_size)) {
					std::cerr << "[오류] 잘못된 패킷 크기: " << (int)packet_size << "\n";
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