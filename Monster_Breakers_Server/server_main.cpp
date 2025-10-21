#include "common.h"
#include "workerthread.h"

//------------------- Check List --------------------- 
// 
//----------------------------------------------------


int main()
{
	std::wcout.imbue(std::locale("korean"));

	WSADATA WSAData;
	if (WSAStartup(MAKEWORD(2, 0), &WSAData) != 0) {
		std::cerr << "[ERROR] WSAStartup ����" << std::endl;
		return 1;
	}


	// 1. ������ ���� ����
	g_listen_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, 0, 0, WSA_FLAG_OVERLAPPED);
	if (g_listen_socket <= 0) std::cout << "ERROR" << "����";
	else std::cout << "Socket Created.\n";

	SOCKADDR_IN addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(SERVER_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	bind(g_listen_socket, reinterpret_cast<sockaddr*>(&addr), sizeof(SOCKADDR_IN));
	listen(g_listen_socket, SOMAXCONN);
	INT addr_size = sizeof(SOCKADDR_IN);

	// 2. IOCP ���� �� ������ ���� ����
	g_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, 0);
	CreateIoCompletionPort(reinterpret_cast<HANDLE>(g_listen_socket), g_hIOCP, -1, 0);


	// 3. �ʱ� Accept ����
	do_accept(g_listen_socket);

	std::cout << "���� ����" << std::endl;
	auto num_threads = (std::min)(8u, std::thread::hardware_concurrency());
	std::vector<std::thread> workers;

	for (unsigned int i = 0; i < num_threads; ++i)
		workers.emplace_back(WorkerThread);
	for (auto& w : workers)
		w.join();


	closesocket(g_listen_socket);
	WSACleanup();
}