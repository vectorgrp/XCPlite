// xcp_test - Sends an XCP CONNECT command to a XCP on UDP server and prints the response
// Helpfull to initially test connectivity to a new XCP server and analyze/understand the XCP transport layer framing and CONNECT response structure.
//
// Build:
//   cmake -B build -S . -DXCPLITE_BUILD_TESTS=ON && cmake --build build --target xcp_test
// Run (requires hello_xcp or similar running on UDP port 5555):
//   ./build/xcp_test [server_ip] [timeout_ms|none]
//
// Arguments:
//   server_ip        Optional. Target IPv4 address. Default: 192.168.0.207
//   timeout_ms|none  Optional. Receive timeout in milliseconds.
//                    Use 'none' or '0' to disable timeout for server-side debugging.
//
// Examples:
//   ./build/xcp_test
//   ./build/xcp_test 127.0.0.1
//   ./build/xcp_test 192.168.0.207 10000
//   ./build/xcp_test 192.168.0.207 none

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET socket_t;
#define CLOSE_SOCKET(s) closesocket(s)
#define SOCKET_INVALID INVALID_SOCKET
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int socket_t;
#define CLOSE_SOCKET(s) close(s)
#define SOCKET_INVALID (-1)
#endif

#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// XCP transport-layer header (4 bytes)  +  payload
//
//  Byte 0-1  LEN  : payload length (little-endian)
//  Byte 2-3  CTR  : packet counter  (little-endian)
//  Byte 4+   data : XCP payload
//
// XCP CONNECT command (0xFF):
//  Byte 0  PID  0xFF
//  Byte 1  mode 0x00  (normal)
//
// XCP CONNECT positive response (0xFF):
//  Byte 0  PID          0xFF
//  Byte 1  resource     (CALPAGE | DAQ | STIM | PGM flags)
//  Byte 2  comm_mode    (BYTE_ORDER | ADDRESS_GRANULARITY | optional modes)
//  Byte 3  max_cto_size
//  Byte 4-5 max_dto_size (little-endian)
//  Byte 6  protocol_version
//  Byte 7  transport_layer_version
// ---------------------------------------------------------------------------

static constexpr uint16_t XCP_SERVER_PORT = 5555;
static constexpr const char *XCP_SERVER_ADDR = "192.168.0.207";
static constexpr int RECV_TIMEOUT_MS = 3000;

static constexpr uint8_t CC_CONNECT = 0xFF;
static constexpr uint8_t CC_GET_COMM_MODE_INFO = 0xFB;
static constexpr uint8_t CC_GET_VERSION = 0xC0;

struct RxMessage {
    std::vector<uint8_t> payload;
    uint16_t tl_len = 0;
    uint16_t tl_ctr = 0;
};

static void print_usage(const char *exe) {
    std::cout << "Usage:\n";
    std::cout << "  " << exe << " [server_ip] [timeout_ms|none]\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  server_ip        Optional target IPv4 address.\n";
    std::cout << "                   Default: " << XCP_SERVER_ADDR << "\n";
    std::cout << "  timeout_ms|none  Optional receive timeout.\n";
    std::cout << "                   Integer milliseconds, or 'none'/'0' to disable timeout.\n";
    std::cout << "                   Default: " << RECV_TIMEOUT_MS << " ms\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << exe << "\n";
    std::cout << "  " << exe << " 127.0.0.1\n";
    std::cout << "  " << exe << " 192.168.0.207 10000\n";
    std::cout << "  " << exe << " 192.168.0.207 none\n";
}

static std::vector<uint8_t> build_xcp_packet(uint16_t ctr, const std::vector<uint8_t> &payload) {
    std::vector<uint8_t> pkt;
    pkt.resize(4 + payload.size());

    const uint16_t len = static_cast<uint16_t>(payload.size());
    pkt[0] = static_cast<uint8_t>(len & 0xFF);
    pkt[1] = static_cast<uint8_t>((len >> 8) & 0xFF);
    pkt[2] = static_cast<uint8_t>(ctr & 0xFF);
    pkt[3] = static_cast<uint8_t>((ctr >> 8) & 0xFF);
    std::memcpy(pkt.data() + 4, payload.data(), payload.size());

    return pkt;
}

// Hex-dump helper
static void print_hex(const uint8_t *data, int len) {
    for (int i = 0; i < len; ++i)
        std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]) << ' ';
    std::cout << std::dec << '\n';
}

static bool receive_xcp_response(socket_t sock, RxMessage &rx, bool timeout_enabled, int timeout_ms) {
    uint8_t buf[256]{};
    sockaddr_in from{};
#ifdef _WIN32
    int from_len = sizeof(from);
#else
    socklen_t from_len = sizeof(from);
#endif

    int received = static_cast<int>(recvfrom(sock, reinterpret_cast<char *>(buf), sizeof(buf), 0, reinterpret_cast<sockaddr *>(&from), &from_len));
    if (received <= 0) {
        if (timeout_enabled) {
            std::cerr << "recvfrom() failed or timeout (no response within " << timeout_ms << " ms)\n";
        } else {
            std::cerr << "recvfrom() failed\n";
        }
        return false;
    }

    std::cout << "RX raw bytes (" << received << " bytes): ";
    print_hex(buf, received);

    if (received < 5) {
        std::cerr << "Response too short to contain a valid XCP transport-layer header\n";
        return false;
    }

    rx.tl_len = static_cast<uint16_t>(buf[0]) | (static_cast<uint16_t>(buf[1]) << 8);
    rx.tl_ctr = static_cast<uint16_t>(buf[2]) | (static_cast<uint16_t>(buf[3]) << 8);

    const int udp_payload_len = received - 4;
    std::cout << "Transport layer header:\n";
    std::cout << "  payload_len : " << rx.tl_len << " bytes\n";
    std::cout << "  counter     : " << rx.tl_ctr << " (0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << rx.tl_ctr << ")\n"
              << std::dec;
    std::cout << "  udp_payload : " << udp_payload_len << " bytes\n";

    if (udp_payload_len != static_cast<int>(rx.tl_len)) {
        const int diff = udp_payload_len - static_cast<int>(rx.tl_len);
        std::cout << "  [WARN] Header payload_len and UDP payload differ by " << diff << " byte(s), decoding with available minimum length\n";
    }

    int decode_len = udp_payload_len;
    if (decode_len > static_cast<int>(rx.tl_len)) {
        decode_len = static_cast<int>(rx.tl_len);
    }
    if (decode_len < 0) {
        decode_len = 0;
    }

    rx.payload.assign(buf + 4, buf + 4 + decode_len);
    return true;
}

// Decode and print the CONNECT response payload (starts at byte 4 of the UDP datagram)
static void decode_connect_response(const uint8_t *payload, int payload_len) {
    if (payload_len < 1) {
        std::cout << "  [ERROR] response payload too short\n";
        return;
    }

    uint8_t pid = payload[0];
    if (pid == 0xFE) {
        // Negative response
        uint8_t error_code = (payload_len >= 2) ? payload[1] : 0;
        std::cout << "  PID          : 0xFE  (NEGATIVE_RESPONSE)\n";
        std::cout << "  Error code   : 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(error_code) << '\n';
        return;
    }

    if (pid != 0xFF) {
        std::cout << "  [ERROR] unexpected PID 0x" << std::hex << static_cast<int>(pid) << '\n';
        return;
    }
    if (payload_len < 8) {
        std::cout << "  [ERROR] CONNECT response too short (" << payload_len << " bytes)\n";
        return;
    }

    uint8_t resource = payload[1];
    uint8_t comm_mode = payload[2];
    uint8_t max_cto = payload[3];
    uint16_t max_dto = static_cast<uint16_t>(payload[4]) | (static_cast<uint16_t>(payload[5]) << 8);
    uint8_t proto_ver = payload[6];
    uint8_t tl_ver = payload[7];

    // Decode resource flags
    std::string res_str;
    if (resource & 0x01)
        res_str += "CALPAGE ";
    if (resource & 0x04)
        res_str += "DAQ ";
    if (resource & 0x08)
        res_str += "STIM ";
    if (resource & 0x10)
        res_str += "PGM ";
    if (res_str.empty())
        res_str = "(none)";

    // Decode byte order from comm_mode bit 0
    std::string byte_order = (comm_mode & 0x01) ? "BIG_ENDIAN" : "LITTLE_ENDIAN";

    // Address granularity from bits 1-2
    uint8_t gran = (comm_mode >> 1) & 0x03;
    std::string gran_str;
    switch (gran) {
    case 0:
        gran_str = "BYTE";
        break;
    case 1:
        gran_str = "WORD";
        break;
    case 2:
        gran_str = "DWORD";
        break;
    default:
        gran_str = "?";
        break;
    }

    std::cout << "  PID                      : 0xFF  (POSITIVE_RESPONSE)\n";
    std::cout << "  resources                : 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(resource) << "  (" << res_str << ")\n";
    std::cout << "  comm_mode_basic          : 0x" << std::setw(2) << static_cast<int>(comm_mode) << "  (byte_order=" << byte_order << ", addr_gran=" << gran_str << ")\n";
    std::cout << "  max_cto_size             : " << std::dec << static_cast<int>(max_cto) << " bytes\n";
    std::cout << "  max_dto_size             : " << max_dto << " bytes\n";
    std::cout << "  protocol_version         : 0x" << std::hex << std::setw(2) << static_cast<int>(proto_ver) << '\n';
    std::cout << "  transport_layer_version  : 0x" << std::setw(2) << static_cast<int>(tl_ver) << '\n';
    std::cout << std::dec;
}

static void decode_get_version_response(const uint8_t *payload, int payload_len) {
    if (payload_len < 1) {
        std::cout << "  [ERROR] response payload too short\n";
        return;
    }

    const uint8_t pid = payload[0];
    if (pid == 0xFE) {
        const uint8_t error_code = (payload_len >= 2) ? payload[1] : 0;
        std::cout << "  PID          : 0xFE  (NEGATIVE_RESPONSE)\n";
        std::cout << "  Error code   : 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(error_code) << '\n';
        return;
    }
    if (pid != 0xFF || payload_len < 6) {
        std::cout << "  [ERROR] invalid GET_VERSION response\n";
        return;
    }

    const uint16_t protocol_version = (static_cast<uint16_t>(payload[2]) << 8) | payload[3];
    const uint16_t transport_layer_version = (static_cast<uint16_t>(payload[4]) << 8) | payload[5];
    std::cout << "  PID                      : 0xFF  (POSITIVE_RESPONSE)\n";
    std::cout << "  protocol_version         : 0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << protocol_version << '\n';
    std::cout << "  transport_layer_version  : 0x" << std::setw(4) << transport_layer_version << '\n';
    std::cout << std::dec;
}

static void decode_get_comm_mode_info_response(const uint8_t *payload, int payload_len) {
    if (payload_len < 1) {
        std::cout << "  [ERROR] response payload too short\n";
        return;
    }

    const uint8_t pid = payload[0];
    if (pid == 0xFE) {
        const uint8_t error_code = (payload_len >= 2) ? payload[1] : 0;
        std::cout << "  PID          : 0xFE  (NEGATIVE_RESPONSE)\n";
        std::cout << "  Error code   : 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(error_code) << '\n';
        return;
    }
    if (pid != 0xFF || payload_len < 8) {
        std::cout << "  [ERROR] invalid GET_COMM_MODE_INFO response\n";
        return;
    }

    const uint8_t comm_mode_optional = payload[2];
    const uint8_t max_bs = payload[3];
    const uint8_t min_st = payload[4];
    const uint8_t queue_size = payload[5];
    const uint8_t xcp_driver_version_number = payload[7];

    std::cout << "  PID                      : 0xFF  (POSITIVE_RESPONSE)\n";
    std::cout << "  comm_mode_optional       : 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
              << static_cast<int>(comm_mode_optional) << '\n';
    std::cout << "  max_bs                   : " << std::dec << static_cast<int>(max_bs) << '\n';
    std::cout << "  min_st                   : " << static_cast<int>(min_st) << '\n';
    std::cout << "  queue_size               : " << static_cast<int>(queue_size) << '\n';
    std::cout << "  driver_version           : 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
              << static_cast<int>(xcp_driver_version_number) << '\n';
    std::cout << std::dec;
}

int main(int argc, char **argv) {
    if (argc > 1 && argv[1] != nullptr && (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0)) {
        print_usage((argc > 0 && argv[0] != nullptr) ? argv[0] : "xcp_test");
        return 0;
    }

#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    // Create UDP socket
    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == SOCKET_INVALID) {
        std::cerr << "socket() failed\n";
        return 1;
    }

    // Bind to any local address (OS picks a port)
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0;
    if (bind(sock, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0) {
        std::cerr << "bind() failed\n";
        CLOSE_SOCKET(sock);
        return 1;
    }

    // Print the actual UDP source port selected for the client socket.
    sockaddr_in local_bound{};
#ifdef _WIN32
    int local_bound_len = sizeof(local_bound);
#else
    socklen_t local_bound_len = sizeof(local_bound);
#endif
    if (getsockname(sock, reinterpret_cast<sockaddr *>(&local_bound), &local_bound_len) == 0) {
        std::cout << "Client UDP source port: " << ntohs(local_bound.sin_port) << '\n';
    } else {
        std::cout << "Client UDP source port: unknown (getsockname failed)\n";
    }

    // Server address (default can be overridden by first command-line argument)
    const char *server_ip = (argc > 1 && argv[1] != nullptr && argv[1][0] != '\0') ? argv[1] : XCP_SERVER_ADDR;

    // Optional timeout (second argument): milliseconds, or "none" / "0" to disable timeout.
    bool timeout_enabled = true;
    int timeout_ms = RECV_TIMEOUT_MS;
    if (argc > 2 && argv[2] != nullptr && argv[2][0] != '\0') {
        if (std::strcmp(argv[2], "none") == 0 || std::strcmp(argv[2], "0") == 0) {
            timeout_enabled = false;
        } else {
            char *end_ptr = nullptr;
            long parsed = std::strtol(argv[2], &end_ptr, 10);
            if (end_ptr == argv[2] || *end_ptr != '\0' || parsed < 0 || parsed > 2147483647L) {
                std::cerr << "Invalid timeout argument '" << argv[2] << "'. Use milliseconds or 'none'.\n";
                CLOSE_SOCKET(sock);
                return 1;
            }
            timeout_ms = static_cast<int>(parsed);
            timeout_enabled = (timeout_ms > 0);
        }
    }

    // Set receive timeout (optional)
#ifdef _WIN32
    DWORD timeout_opt = timeout_enabled ? static_cast<DWORD>(timeout_ms) : 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout_opt), sizeof(timeout_opt));
#else
    timeval tv{};
    if (timeout_enabled) {
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
    } else {
        tv.tv_sec = 0;
        tv.tv_usec = 0;
    }
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    if (timeout_enabled) {
        std::cout << "Receive timeout: " << timeout_ms << " ms\n";
    } else {
        std::cout << "Receive timeout: disabled\n";
    }

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(XCP_SERVER_PORT);
    if (inet_pton(AF_INET, server_ip, &server.sin_addr) != 1) {
        std::cerr << "inet_pton() failed for " << server_ip << '\n';
        CLOSE_SOCKET(sock);
        return 1;
    }

    uint16_t tx_ctr = 0;
    bool have_prev_rx_ctr = false;
    uint16_t prev_rx_ctr = 0;

    auto send_command_and_receive = [&](const char *name, const std::vector<uint8_t> &payload, RxMessage &rx) -> bool {
        const auto pkt = build_xcp_packet(tx_ctr, payload);
        std::cout << "\nSending XCP " << name << " to " << server_ip << ":" << XCP_SERVER_PORT << " (UDP), tx_ctr=" << tx_ctr << "\n";
        std::cout << "TX raw bytes: ";
        print_hex(pkt.data(), static_cast<int>(pkt.size()));

        int sent = static_cast<int>(sendto(sock, reinterpret_cast<const char *>(pkt.data()), static_cast<int>(pkt.size()), 0, reinterpret_cast<const sockaddr *>(&server), sizeof(server)));
        if (sent != static_cast<int>(pkt.size())) {
            std::cerr << "sendto() failed (sent=" << sent << ")\n";
            return false;
        }

        if (!receive_xcp_response(sock, rx, timeout_enabled, timeout_ms)) {
            return false;
        }

        if (have_prev_rx_ctr) {
            const uint16_t expected = static_cast<uint16_t>(prev_rx_ctr + 1);
            if (rx.tl_ctr == expected) {
                std::cout << "Counter check: OK (" << prev_rx_ctr << " -> " << rx.tl_ctr << ")\n";
            } else {
                std::cout << "Counter check: [WARN] unexpected jump (prev=" << prev_rx_ctr << ", current=" << rx.tl_ctr << ", expected=" << expected << ")\n";
            }
        } else {
            std::cout << "Counter check: first response counter = " << rx.tl_ctr << "\n";
        }
        prev_rx_ctr = rx.tl_ctr;
        have_prev_rx_ctr = true;

        tx_ctr = static_cast<uint16_t>(tx_ctr + 1);
        return true;
    };

    RxMessage connect_rx;
    if (!send_command_and_receive("CONNECT", {CC_CONNECT, 0x00}, connect_rx)) {
        CLOSE_SOCKET(sock);
        return 1;
    }
    std::cout << "XCP CONNECT response decoded:\n";
    decode_connect_response(connect_rx.payload.data(), static_cast<int>(connect_rx.payload.size()));

    uint8_t comm_mode_basic = 0;
    bool connect_positive = connect_rx.payload.size() >= 3 && connect_rx.payload[0] == 0xFF;
    if (connect_positive) {
        comm_mode_basic = connect_rx.payload[2];
    }

    RxMessage get_version_rx;
    if (!send_command_and_receive("GET_VERSION", {CC_GET_VERSION, 0x00}, get_version_rx)) {
        CLOSE_SOCKET(sock);
        return 1;
    }
    std::cout << "XCP GET_VERSION response decoded:\n";
    decode_get_version_response(get_version_rx.payload.data(), static_cast<int>(get_version_rx.payload.size()));

    if (connect_positive && ((comm_mode_basic & 0x80) != 0)) {
        RxMessage get_comm_mode_info_rx;
        if (!send_command_and_receive("GET_COMM_MODE_INFO", {CC_GET_COMM_MODE_INFO, 0x00}, get_comm_mode_info_rx)) {
            CLOSE_SOCKET(sock);
            return 1;
        }
        std::cout << "XCP GET_COMM_MODE_INFO response decoded:\n";
        decode_get_comm_mode_info_response(get_comm_mode_info_rx.payload.data(), static_cast<int>(get_comm_mode_info_rx.payload.size()));
    } else {
        std::cout << "\nSkipping GET_COMM_MODE_INFO because CONNECT response indicates it is not supported (comm_mode_basic bit7 not set).\n";
    }

    CLOSE_SOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
