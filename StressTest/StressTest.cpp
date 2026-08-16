#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <Psapi.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Psapi.lib")

namespace {

constexpr char CS_P_LOGIN = 5;
constexpr char CS_P_MOVE = 6;
constexpr char CS_P_LOADING_DONE = 25;
constexpr char CS_P_STRESS_PING = 90;
constexpr char SC_P_STRESS_PONG = 91;
constexpr int MAX_ID_LENGTH = 20;
constexpr std::size_t RECV_BUFFER_SIZE = 8192;
constexpr std::uint64_t MEASURED_PING_BIT = (1ull << 63);

#pragma pack(push, 1)
struct Float3 { float x, y, z; };

struct LoginPacket {
    unsigned char size = sizeof(LoginPacket);
    char type = CS_P_LOGIN;
    char name[MAX_ID_LENGTH]{};
    std::uint8_t job = 0;
};

struct LoadingDonePacket {
    unsigned char size = sizeof(LoadingDonePacket);
    char type = CS_P_LOADING_DONE;
};

struct MovePacket {
    unsigned char size = sizeof(MovePacket);
    char type = CS_P_MOVE;
    Float3 position{};
    Float3 look{0.0f, 0.0f, 1.0f};
    Float3 right{1.0f, 0.0f, 0.0f};
    std::uint8_t animState = 1;
};

struct StressPingPacket {
    unsigned char size = sizeof(StressPingPacket);
    char type = CS_P_STRESS_PING;
    std::uint64_t sequence = 0;
    std::int64_t clientTimestampNs = 0;
};

struct StressPongPacket {
    unsigned char size;
    char type;
    std::uint64_t sequence;
    std::int64_t clientTimestampNs;
};
#pragma pack(pop)

static_assert(sizeof(LoginPacket) == 23);
static_assert(sizeof(LoadingDonePacket) == 2);
static_assert(sizeof(MovePacket) == 39);
static_assert(sizeof(StressPingPacket) == 18);

struct Config {
    std::string host = "127.0.0.1";
    std::uint16_t port = 3000;
    int clients = 1000;
    int durationSeconds = 60;
    int rampSeconds = 10;
    int warmupSeconds = 30;
    int moveHz = 2;
    int pingHz = 1;
    unsigned long serverPid = 0;
    std::string csvPath = "Results/stress_result.csv";
};

struct Metrics {
    std::atomic<std::uint64_t> connectAttempts{0};
    std::atomic<std::uint64_t> connected{0};
    std::atomic<std::uint64_t> connectFailed{0};
    std::atomic<std::uint64_t> disconnected{0};
    std::atomic<std::uint64_t> packetsSent{0};
    std::atomic<std::uint64_t> packetsReceived{0};
    std::atomic<std::uint64_t> bytesSent{0};
    std::atomic<std::uint64_t> bytesReceived{0};
    std::atomic<std::uint64_t> sendErrors{0};
    std::atomic<std::uint64_t> invalidPackets{0};
    std::atomic<std::uint64_t> pingsSent{0};
    std::atomic<std::uint64_t> pongsReceived{0};
    std::mutex latencyMutex;
    std::vector<double> latencyMs;
};

struct LatencySummary {
    double average = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double maximum = 0.0;
};

struct ServerResourceSample {
    double cpuPercent = -1.0;
    double memoryMb = -1.0;
};

class ServerMonitor {
public:
    bool Open(unsigned long pid) {
        if (pid == 0) return false;
        process_ = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
        if (!process_) return false;
        SYSTEM_INFO info{};
        GetSystemInfo(&info);
        processors_ = (std::max)(1u, static_cast<unsigned int>(info.dwNumberOfProcessors));
        FILETIME creation{}, exit{}, kernel{}, user{};
        if (!GetProcessTimes(process_, &creation, &exit, &kernel, &user)) return false;
        previousProcess100ns_ = ToU64(kernel) + ToU64(user);
        previousWall_ = std::chrono::steady_clock::now();
        return true;
    }

    ServerResourceSample Sample() {
        ServerResourceSample sample;
        if (!process_) return sample;
        PROCESS_MEMORY_COUNTERS_EX memory{};
        memory.cb = sizeof(memory);
        if (GetProcessMemoryInfo(process_, reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory), sizeof(memory)))
            sample.memoryMb = memory.WorkingSetSize / 1048576.0;
        FILETIME creation{}, exit{}, kernel{}, user{};
        const auto now = std::chrono::steady_clock::now();
        if (GetProcessTimes(process_, &creation, &exit, &kernel, &user)) {
            const std::uint64_t current = ToU64(kernel) + ToU64(user);
            const double wall100ns = std::chrono::duration<double>(now - previousWall_).count() * 10000000.0;
            if (wall100ns > 0.0)
                sample.cpuPercent = (current - previousProcess100ns_) / wall100ns * 100.0 / processors_;
            previousProcess100ns_ = current;
            previousWall_ = now;
        }
        return sample;
    }

    ~ServerMonitor() { if (process_) CloseHandle(process_); }

private:
    static std::uint64_t ToU64(FILETIME value) {
        ULARGE_INTEGER result{};
        result.LowPart = value.dwLowDateTime;
        result.HighPart = value.dwHighDateTime;
        return result.QuadPart;
    }
    HANDLE process_ = nullptr;
    unsigned int processors_ = 1;
    std::uint64_t previousProcess100ns_ = 0;
    std::chrono::steady_clock::time_point previousWall_{};
};

struct Client;

struct ReceiveContext {
    OVERLAPPED overlapped{};
    WSABUF wsabuf{};
    char buffer[RECV_BUFFER_SIZE]{};
};

struct Client {
    SOCKET socket = INVALID_SOCKET;
    int index = 0;
    std::atomic<bool> alive{false};
    ReceiveContext receive{};
    std::vector<unsigned char> pending;
};

Config g_config;
Metrics g_metrics;
HANDLE g_iocp = nullptr;
std::atomic<bool> g_running{true};
std::vector<std::unique_ptr<Client>> g_clients;

void PrintUsage() {
    std::cout
        << "Monster Breakers IOCP Stress Test\n\n"
        << "Usage:\n"
        << "  StressTest.exe [options]\n\n"
        << "Options:\n"
        << "  --host <ip>       Server IP (default: 127.0.0.1)\n"
        << "  --port <number>   Server port (default: 3000)\n"
        << "  --clients <count> Virtual clients (default: 100)\n"
        << "  --duration <sec>  Test duration after ramp-up (default: 60)\n"
        << "  --ramp <sec>      Time to connect all clients (default: 10)\n"
        << "  --warmup <sec>    Active-load warm-up excluded from RTT stats (default: 30)\n"
        << "  --move-hz <rate>  Move packets per client/second (default: 2)\n"
        << "  --ping-hz <rate>  RTT probes per client/second (default: 1)\n"
        << "  --server-pid <id> Local server process ID for CPU/RAM metrics\n"
        << "  --csv <path>      Result CSV path\n"
        << "  --help            Show this help\n";
}

bool ParsePositive(const char* text, int& value, bool allowZero = false) {
    try {
        std::size_t used = 0;
        const int parsed = std::stoi(text, &used);
        if (text[used] != '\0' || parsed < (allowZero ? 0 : 1)) return false;
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool ParseArguments(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto next = [&]() -> const char* { return (i + 1 < argc) ? argv[++i] : nullptr; };
        if (arg == "--help") { PrintUsage(); return false; }
        if (arg == "--host") {
            const char* value = next(); if (!value) return false; g_config.host = value;
        } else if (arg == "--port") {
            int value = 0; const char* text = next();
            if (!text || !ParsePositive(text, value) || value > 65535) return false;
            g_config.port = static_cast<std::uint16_t>(value);
        } else if (arg == "--clients") {
            const char* value = next(); if (!value || !ParsePositive(value, g_config.clients)) return false;
        } else if (arg == "--duration") {
            const char* value = next(); if (!value || !ParsePositive(value, g_config.durationSeconds)) return false;
        } else if (arg == "--ramp") {
            const char* value = next(); if (!value || !ParsePositive(value, g_config.rampSeconds, true)) return false;
        } else if (arg == "--warmup") {
            const char* value = next(); if (!value || !ParsePositive(value, g_config.warmupSeconds, true)) return false;
        } else if (arg == "--move-hz") {
            const char* value = next(); if (!value || !ParsePositive(value, g_config.moveHz, true)) return false;
        } else if (arg == "--ping-hz") {
            const char* value = next(); if (!value || !ParsePositive(value, g_config.pingHz, true)) return false;
        } else if (arg == "--server-pid") {
            int value = 0; const char* text = next();
            if (!text || !ParsePositive(text, value)) return false;
            g_config.serverPid = static_cast<unsigned long>(value);
        } else if (arg == "--csv") {
            const char* value = next(); if (!value) return false; g_config.csvPath = value;
        } else {
            std::cerr << "Unknown option: " << arg << '\n'; return false;
        }
    }
    return true;
}

bool SendAllBlocking(SOCKET socket, const char* data, int length) {
    int sent = 0;
    while (sent < length) {
        const int result = send(socket, data + sent, length - sent, 0);
        if (result == SOCKET_ERROR || result == 0) return false;
        sent += result;
    }
    g_metrics.packetsSent.fetch_add(1, std::memory_order_relaxed);
    g_metrics.bytesSent.fetch_add(sent, std::memory_order_relaxed);
    return true;
}

bool PostReceive(Client& client) {
    ZeroMemory(&client.receive.overlapped, sizeof(OVERLAPPED));
    client.receive.wsabuf.buf = client.receive.buffer;
    client.receive.wsabuf.len = static_cast<ULONG>(sizeof(client.receive.buffer));
    DWORD flags = 0;
    const int result = WSARecv(client.socket, &client.receive.wsabuf, 1, nullptr, &flags,
                               &client.receive.overlapped, nullptr);
    return result != SOCKET_ERROR || WSAGetLastError() == WSA_IO_PENDING;
}

void CountPackets(Client& client, const char* data, DWORD length) {
    client.pending.insert(client.pending.end(), data, data + length);
    std::size_t offset = 0;
    while (offset < client.pending.size()) {
        const unsigned int packetSize = client.pending[offset];
        if (packetSize < 2 || packetSize > 255) {
            g_metrics.invalidPackets.fetch_add(1, std::memory_order_relaxed);
            client.pending.clear();
            return;
        }
        if (client.pending.size() - offset < packetSize) break;
        const unsigned char packetType = client.pending[offset + 1];
        if (packetType == static_cast<unsigned char>(SC_P_STRESS_PONG)
            && packetSize == sizeof(StressPongPacket)) {
            StressPongPacket pong{};
            std::memcpy(&pong, client.pending.data() + offset, sizeof(pong));
            const auto nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            const double latency = (nowNs - pong.clientTimestampNs) / 1000000.0;
            if ((pong.sequence & MEASURED_PING_BIT) != 0 && latency >= 0.0 && latency < 60000.0) {
                std::lock_guard<std::mutex> lock(g_metrics.latencyMutex);
                g_metrics.latencyMs.push_back(latency);
                g_metrics.pongsReceived.fetch_add(1, std::memory_order_relaxed);
            }
        }
        g_metrics.packetsReceived.fetch_add(1, std::memory_order_relaxed);
        offset += packetSize;
    }
    if (offset > 0) client.pending.erase(client.pending.begin(), client.pending.begin() + offset);
    if (client.pending.size() > RECV_BUFFER_SIZE * 2) {
        g_metrics.invalidPackets.fetch_add(1, std::memory_order_relaxed);
        client.pending.clear();
    }
}

void MarkDisconnected(Client& client) {
    if (client.alive.exchange(false, std::memory_order_relaxed)) {
        g_metrics.disconnected.fetch_add(1, std::memory_order_relaxed);
    }
}

void IoWorker() {
    while (true) {
        DWORD transferred = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* overlapped = nullptr;
        const BOOL ok = GetQueuedCompletionStatus(g_iocp, &transferred, &key, &overlapped, INFINITE);
        if (!overlapped && key == 0) return;
        Client* client = reinterpret_cast<Client*>(key);
        if (!client) continue;
        if (!ok || transferred == 0) {
            if (g_running.load(std::memory_order_relaxed)) MarkDisconnected(*client);
            continue;
        }
        g_metrics.bytesReceived.fetch_add(transferred, std::memory_order_relaxed);
        CountPackets(*client, client->receive.buffer, transferred);
        if (g_running.load(std::memory_order_relaxed) && client->alive.load(std::memory_order_relaxed)
            && !PostReceive(*client)) {
            MarkDisconnected(*client);
        }
    }
}

bool ConnectClient(Client& client, const sockaddr_in& address) {
    g_metrics.connectAttempts.fetch_add(1, std::memory_order_relaxed);
    client.socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (client.socket == INVALID_SOCKET) return false;

    if (connect(client.socket, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
        closesocket(client.socket); client.socket = INVALID_SOCKET; return false;
    }
    if (!CreateIoCompletionPort(reinterpret_cast<HANDLE>(client.socket), g_iocp,
                                reinterpret_cast<ULONG_PTR>(&client), 0)) {
        closesocket(client.socket); client.socket = INVALID_SOCKET; return false;
    }

    LoginPacket login;
    const std::string name = "Stress_" + std::to_string(client.index);
    strncpy_s(login.name, name.c_str(), _TRUNCATE);
    login.job = static_cast<std::uint8_t>(client.index % 3);
    if (!SendAllBlocking(client.socket, reinterpret_cast<const char*>(&login), sizeof(login))) {
        closesocket(client.socket); client.socket = INVALID_SOCKET; return false;
    }

    // Enter the same game-ready state as the real client. Without this packet the
    // server intentionally drops every CS_P_MOVE packet before game loading ends.
    LoadingDonePacket loadingDone;
    if (!SendAllBlocking(client.socket, reinterpret_cast<const char*>(&loadingDone), sizeof(loadingDone))) {
        closesocket(client.socket); client.socket = INVALID_SOCKET; return false;
    }

    // Do not let one slow server connection stall movement generation for every client.
    u_long nonBlocking = 1;
    if (ioctlsocket(client.socket, FIONBIO, &nonBlocking) == SOCKET_ERROR) {
        closesocket(client.socket); client.socket = INVALID_SOCKET; return false;
    }

    client.alive.store(true, std::memory_order_relaxed);
    if (!PostReceive(client)) {
        client.alive.store(false, std::memory_order_relaxed);
        closesocket(client.socket); client.socket = INVALID_SOCKET; return false;
    }
    g_metrics.connected.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void SendMovement(Client& client, double elapsedSeconds) {
    if (!client.alive.load(std::memory_order_relaxed)) return;
    MovePacket packet;
    const float angle = static_cast<float>(elapsedSeconds * 0.7 + client.index * 0.13);
    const float radius = 2.0f + static_cast<float>(client.index % 20) * 0.25f;
    packet.position = {std::cos(angle) * radius, 0.0f, std::sin(angle) * radius};
    packet.look = {-std::sin(angle), 0.0f, std::cos(angle)};
    packet.right = {packet.look.z, 0.0f, -packet.look.x};

    const int result = send(client.socket, reinterpret_cast<const char*>(&packet), sizeof(packet), 0);
    if (result == sizeof(packet)) {
        g_metrics.packetsSent.fetch_add(1, std::memory_order_relaxed);
        g_metrics.bytesSent.fetch_add(result, std::memory_order_relaxed);
    } else {
        g_metrics.sendErrors.fetch_add(1, std::memory_order_relaxed);
        if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) MarkDisconnected(client);
    }
}

void SendPing(Client& client, std::uint64_t sequence, bool measured) {
    if (!client.alive.load(std::memory_order_relaxed)) return;
    StressPingPacket packet;
    packet.sequence = measured ? (sequence | MEASURED_PING_BIT) : sequence;
    packet.clientTimestampNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const int result = send(client.socket, reinterpret_cast<const char*>(&packet), sizeof(packet), 0);
    if (result == sizeof(packet)) {
        g_metrics.packetsSent.fetch_add(1, std::memory_order_relaxed);
        g_metrics.pingsSent.fetch_add(1, std::memory_order_relaxed);
        g_metrics.bytesSent.fetch_add(result, std::memory_order_relaxed);
    } else {
        g_metrics.sendErrors.fetch_add(1, std::memory_order_relaxed);
        if (result == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK) MarkDisconnected(client);
    }
}

void ResetMeasurementTraffic() {
    g_metrics.packetsSent.store(0, std::memory_order_relaxed);
    g_metrics.packetsReceived.store(0, std::memory_order_relaxed);
    g_metrics.bytesSent.store(0, std::memory_order_relaxed);
    g_metrics.bytesReceived.store(0, std::memory_order_relaxed);
    g_metrics.pingsSent.store(0, std::memory_order_relaxed);
    g_metrics.pongsReceived.store(0, std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_metrics.latencyMutex);
    g_metrics.latencyMs.clear();
}

LatencySummary GetLatencySummary() {
    std::vector<double> values;
    {
        std::lock_guard<std::mutex> lock(g_metrics.latencyMutex);
        values = g_metrics.latencyMs;
    }
    LatencySummary result;
    if (values.empty()) return result;
    std::sort(values.begin(), values.end());
    double sum = 0.0;
    for (double value : values) sum += value;
    auto percentile = [&](double p) {
        const std::size_t index = static_cast<std::size_t>(std::ceil(p * values.size())) - 1;
        return values[(std::min)(index, values.size() - 1)];
    };
    result.average = sum / values.size();
    result.p50 = percentile(0.50);
    result.p95 = percentile(0.95);
    result.p99 = percentile(0.99);
    result.maximum = values.back();
    return result;
}

void WriteCsvHeader(std::ofstream& csv) {
    csv << "elapsed_sec,active_clients,connected_total,connect_failed,disconnected,"
           "packets_sent,packets_received,bytes_sent,bytes_received,send_errors,invalid_packets,"
           "pings_sent,pongs_received,rtt_avg_ms,rtt_p50_ms,rtt_p95_ms,rtt_p99_ms,rtt_max_ms,"
           "server_cpu_percent,server_memory_mb,target_clients,move_hz,ping_hz,warmup_sec,server_pid\n";
}

void WriteCsvRow(std::ofstream& csv, int elapsed, const ServerResourceSample& resources) {
    const auto connected = g_metrics.connected.load();
    const auto disconnected = g_metrics.disconnected.load();
    const LatencySummary latency = GetLatencySummary();
    csv << elapsed << ',' << (connected - disconnected) << ',' << connected << ','
        << g_metrics.connectFailed.load() << ',' << disconnected << ','
        << g_metrics.packetsSent.load() << ',' << g_metrics.packetsReceived.load() << ','
        << g_metrics.bytesSent.load() << ',' << g_metrics.bytesReceived.load() << ','
        << g_metrics.sendErrors.load() << ',' << g_metrics.invalidPackets.load() << ','
        << g_metrics.pingsSent.load() << ',' << g_metrics.pongsReceived.load() << ','
        << std::fixed << std::setprecision(3) << latency.average << ',' << latency.p50 << ','
        << latency.p95 << ',' << latency.p99 << ',' << latency.maximum << ','
        << resources.cpuPercent << ',' << resources.memoryMb << ','
        << g_config.clients << ',' << g_config.moveHz << ',' << g_config.pingHz << ','
        << g_config.warmupSeconds << ',' << g_config.serverPid << '\n';
    csv.flush();
}

void PrintStatus(int elapsed) {
    const auto connected = g_metrics.connected.load();
    const auto disconnected = g_metrics.disconnected.load();
    std::cout << "[" << std::setw(4) << elapsed << "s] active=" << (connected - disconnected)
              << " connected=" << connected << " failed=" << g_metrics.connectFailed.load()
              << " sent=" << g_metrics.packetsSent.load()
              << " recv=" << g_metrics.packetsReceived.load()
              << " p95=" << std::fixed << std::setprecision(2) << GetLatencySummary().p95 << "ms"
              << " recvMB=" << std::fixed << std::setprecision(1)
              << (g_metrics.bytesReceived.load() / 1048576.0) << '\n';
}

} // namespace

int main(int argc, char** argv) {
    if (!ParseArguments(argc, argv)) {
        if (argc > 1 && std::string(argv[1]) == "--help") return 0;
        PrintUsage(); return 1;
    }

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed.\n"; return 1;
    }
    g_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if (!g_iocp) { std::cerr << "CreateIoCompletionPort failed.\n"; WSACleanup(); return 1; }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(g_config.port);
    if (InetPtonA(AF_INET, g_config.host.c_str(), &address.sin_addr) != 1) {
        std::cerr << "--host must be an IPv4 address.\n"; CloseHandle(g_iocp); WSACleanup(); return 1;
    }

    std::error_code ec;
    const std::filesystem::path csvPath(g_config.csvPath);
    if (csvPath.has_parent_path()) std::filesystem::create_directories(csvPath.parent_path(), ec);
    std::ofstream csv(csvPath);
    if (!csv) { std::cerr << "Cannot open CSV: " << g_config.csvPath << '\n'; CloseHandle(g_iocp); WSACleanup(); return 1; }
    WriteCsvHeader(csv);

    ServerMonitor serverMonitor;
    const bool monitoringServer = serverMonitor.Open(g_config.serverPid);
    if (g_config.serverPid != 0 && !monitoringServer)
        std::cerr << "Warning: could not monitor server PID " << g_config.serverPid << ".\n";

    const unsigned int workerCount = (std::max)(2u, (std::min)(8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> workers;
    for (unsigned int i = 0; i < workerCount; ++i) workers.emplace_back(IoWorker);

    g_clients.reserve(g_config.clients);
    for (int i = 0; i < g_config.clients; ++i) {
        auto client = std::make_unique<Client>();
        client->index = i;
        g_clients.push_back(std::move(client));
    }

    std::cout << "Target " << g_config.host << ':' << g_config.port
              << " | clients=" << g_config.clients << " ramp=" << g_config.rampSeconds
              << "s duration=" << g_config.durationSeconds << "s moveHz=" << g_config.moveHz << "\n";

    const auto testStart = std::chrono::steady_clock::now();
    const double connectInterval = g_config.rampSeconds > 0
        ? static_cast<double>(g_config.rampSeconds) / g_config.clients : 0.0;
    for (int i = 0; i < g_config.clients; ++i) {
        const auto due = testStart + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(connectInterval * i));
        if (connectInterval > 0.0) std::this_thread::sleep_until(due);
        if (!ConnectClient(*g_clients[i], address)) {
            g_metrics.connectFailed.fetch_add(1, std::memory_order_relaxed);
        }
    }

    // Real clients do not send their updates on the exact same microsecond. Spread the
    // same total packet rate evenly across the interval to avoid an artificial burst.
    const auto moveDispatchInterval = g_config.moveHz > 0
        ? std::chrono::duration<double>(1.0 / (static_cast<double>(g_config.clients) * g_config.moveHz))
        : std::chrono::duration<double>(3600.0);
    const auto pingDispatchInterval = g_config.pingHz > 0
        ? std::chrono::duration<double>(1.0 / (static_cast<double>(g_config.clients) * g_config.pingHz))
        : std::chrono::duration<double>(3600.0);
    std::uint64_t pingSequence = 0;

    if (g_config.warmupSeconds > 0) {
        const auto warmupStart = std::chrono::steady_clock::now();
        const auto warmupFinish = warmupStart + std::chrono::seconds(g_config.warmupSeconds);
        auto warmupMove = warmupStart;
        auto warmupPing = warmupStart;
        auto warmupReport = warmupStart;
        std::size_t warmupMoveClient = 0;
        std::size_t warmupPingClient = 0;
        std::cout << "Warm-up started: " << g_config.warmupSeconds
                  << "s of active movement load (excluded from RTT statistics).\n";
        while (std::chrono::steady_clock::now() < warmupFinish) {
            const auto now = std::chrono::steady_clock::now();
            while (g_config.moveHz > 0 && now >= warmupMove) {
                const double elapsed = std::chrono::duration<double>(now - warmupStart).count();
                SendMovement(*g_clients[warmupMoveClient], elapsed);
                warmupMoveClient = (warmupMoveClient + 1) % g_clients.size();
                warmupMove += std::chrono::duration_cast<std::chrono::steady_clock::duration>(moveDispatchInterval);
            }
            while (g_config.pingHz > 0 && now >= warmupPing) {
                SendPing(*g_clients[warmupPingClient], ++pingSequence, false);
                warmupPingClient = (warmupPingClient + 1) % g_clients.size();
                warmupPing += std::chrono::duration_cast<std::chrono::steady_clock::duration>(pingDispatchInterval);
            }
            if (now >= warmupReport) {
                const int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - warmupStart).count());
                std::cout << "[warm-up " << elapsed << "s] active="
                          << (g_metrics.connected.load() - g_metrics.disconnected.load()) << '\n';
                serverMonitor.Sample();
                warmupReport = now + std::chrono::seconds(5);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        ResetMeasurementTraffic();
        std::cout << "Warm-up complete. Measurement counters reset.\n";
    }

    const auto loadStart = std::chrono::steady_clock::now();
    auto nextMove = loadStart;
    auto nextPing = loadStart;
    auto nextReport = loadStart;
    std::size_t nextMoveClient = 0;
    std::size_t nextPingClient = 0;
    const auto finish = loadStart + std::chrono::seconds(g_config.durationSeconds);
    while (std::chrono::steady_clock::now() < finish) {
        const auto now = std::chrono::steady_clock::now();
        while (g_config.moveHz > 0 && now >= nextMove) {
            const double elapsed = std::chrono::duration<double>(now - loadStart).count();
            SendMovement(*g_clients[nextMoveClient], elapsed);
            nextMoveClient = (nextMoveClient + 1) % g_clients.size();
            nextMove += std::chrono::duration_cast<std::chrono::steady_clock::duration>(moveDispatchInterval);
        }
        while (g_config.pingHz > 0 && now >= nextPing) {
            SendPing(*g_clients[nextPingClient], ++pingSequence, true);
            nextPingClient = (nextPingClient + 1) % g_clients.size();
            nextPing += std::chrono::duration_cast<std::chrono::steady_clock::duration>(pingDispatchInterval);
        }
        if (now >= nextReport) {
            const int elapsed = static_cast<int>(std::chrono::duration_cast<std::chrono::seconds>(now - loadStart).count());
            PrintStatus(elapsed); WriteCsvRow(csv, elapsed, serverMonitor.Sample());
            nextReport = now + std::chrono::seconds(1);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    WriteCsvRow(csv, g_config.durationSeconds, serverMonitor.Sample());
    g_running.store(false, std::memory_order_relaxed);
    for (auto& client : g_clients) {
        if (client->socket != INVALID_SOCKET) {
            shutdown(client->socket, SD_BOTH);
            closesocket(client->socket);
            client->socket = INVALID_SOCKET;
        }
    }
    for (unsigned int i = 0; i < workerCount; ++i) PostQueuedCompletionStatus(g_iocp, 0, 0, nullptr);
    for (auto& worker : workers) worker.join();

    std::cout << "\nCompleted. connected=" << g_metrics.connected.load()
              << " failed=" << g_metrics.connectFailed.load()
              << " disconnected=" << g_metrics.disconnected.load()
              << " packetsSent=" << g_metrics.packetsSent.load()
              << " packetsReceived=" << g_metrics.packetsReceived.load()
              << "\nCSV: " << std::filesystem::absolute(csvPath).string() << '\n';

    CloseHandle(g_iocp);
    WSACleanup();
    return g_metrics.connectFailed.load() == 0 ? 0 : 2;
}
