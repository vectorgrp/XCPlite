// xcp_test - XCP on UDP client test: command sequence, response decoding and command round trip latency
//
// Hermetic by default: starts an XCP on UDP server in process on 127.0.0.1 and runs the test against it.
// With --server, the test probes an external XCP on UDP server instead, helpfull to initially test connectivity to a new XCP server
// and analyze/understand the XCP transport layer framing and CONNECT response structure.
//
// Test sequence: CONNECT, GET_VERSION, GET_COMM_MODE_INFO (if supported), GET_STATUS latency measurement, DISCONNECT
// The latency is measured back to back (steady state) and after idle time (includes the thread wake-up latency).
// The test fails on any protocol error (timeout, negative response, unexpected PID, framing, counter) and when the
// median back to back latency exceeds the limit. The after idle latency is only reported.
//
// Build:
//   cmake -B build -S . -DXCPLITE_BUILD_TESTS=ON && cmake --build build --target xcp_test
// Run:
//   ./build/xcp_test [options]
//
// Options:
//   --server <ip>         Probe an external XCP on UDP server instead of the in-process server
//   --port <port>         External server port. Default: 5555
//   --timeout <ms|none>   Receive timeout in milliseconds, 'none' or '0' disables the timeout (for server-side debugging). Default: 3000
//   --count <n>           Number of back to back GET_STATUS commands for the latency measurement, '0' skips it. Default: 1000
//   --max-latency <us>    Limit for the median back to back latency, '0' disables the check.
//                         Default: 1000, or the value of the environment variable XCP_TEST_MAX_LATENCY_US
//
// Examples:
//   ./build/xcp_test
//   ./build/xcp_test --count 10000
//   ./build/xcp_test --server 127.0.0.1
//   ./build/xcp_test --server 192.168.0.207 --timeout none

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

#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "xcplib.h" // for the in-process XCP server

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

static constexpr uint16_t XCP_SERVER_PORT = 5555;              // Default port of an external server
static constexpr uint16_t LOCAL_SERVER_PORT = 5599;            // First port tried for the in-process server
static constexpr int LOCAL_SERVER_PORT_TRIES = 20;             // Number of consecutive ports tried for the in-process server
static constexpr uint32_t LOCAL_SERVER_QUEUE_SIZE = 64 * 1024; // Measurement queue size of the in-process server
static constexpr uint8_t LOCAL_SERVER_LOG_LEVEL = 2;           // Warnings only, logging adds latency to every command
static constexpr int RECV_TIMEOUT_MS = 3000;
static constexpr int LATENCY_COUNT = 1000;      // Default number of back to back commands for the latency measurement
static constexpr int LATENCY_WARMUP_COUNT = 10; // Back to back commands not included in the statistics
static constexpr int LATENCY_IDLE_COUNT = 20;   // Number of commands for the latency measurement after idle time
static constexpr int LATENCY_IDLE_MS = 10;      // Idle time before each command
static constexpr int MAX_LATENCY_US = 1000;     // Default limit for the median back to back latency

static constexpr uint8_t CC_CONNECT = 0xFF;
static constexpr uint8_t CC_DISCONNECT = 0xFE;
static constexpr uint8_t CC_GET_STATUS = 0xFD;
static constexpr uint8_t CC_GET_COMM_MODE_INFO = 0xFB;
static constexpr uint8_t CC_GET_VERSION = 0xC0;

static constexpr uint8_t PID_RES = 0xFF;
static constexpr uint8_t PID_ERR = 0xFE;
static constexpr uint8_t PID_EV = 0xFD;
static constexpr uint8_t PID_SERV = 0xFC;

struct Options {
    const char *server_ip = nullptr; // nullptr: in-process server
    uint16_t port = XCP_SERVER_PORT;
    bool timeout_enabled = true;
    int timeout_ms = RECV_TIMEOUT_MS;
    int latency_count = LATENCY_COUNT;
    int max_latency_us = MAX_LATENCY_US;
};

struct Client {
    socket_t sock = SOCKET_INVALID;
    sockaddr_in server{};
    uint16_t tx_ctr = 0;
    bool have_prev_rx_ctr = false;
    uint16_t prev_rx_ctr = 0;
};

struct RxMessage {
    std::vector<uint8_t> payload;
    uint16_t tl_len = 0;
    uint16_t tl_ctr = 0;
};

// Number of errors, the test fails if not zero
static int gErrors = 0;

static void fail(const std::string &msg) {
    std::cerr << "[ERROR] " << msg << '\n';
    gErrors++;
}

static void print_usage(const char *exe) {
    std::cout << "Usage:\n";
    std::cout << "  " << exe << " [options]\n\n";
    std::cout << "Without --server, an XCP on UDP server is started in process on 127.0.0.1 (hermetic self-check).\n\n";
    std::cout << "Options:\n";
    std::cout << "  --server <ip>        Probe an external XCP on UDP server instead of the in-process server\n";
    std::cout << "  --port <port>        External server port. Default: " << XCP_SERVER_PORT << "\n";
    std::cout << "  --timeout <ms|none>  Receive timeout in milliseconds, 'none' or '0' disables the timeout. Default: " << RECV_TIMEOUT_MS << "\n";
    std::cout << "  --count <n>          Number of back to back GET_STATUS commands for the latency measurement, '0' skips it. Default: " << LATENCY_COUNT << "\n";
    std::cout << "  --max-latency <us>   Limit for the median back to back latency, '0' disables the check.\n";
    std::cout << "                       Default: " << MAX_LATENCY_US << ", or the environment variable XCP_TEST_MAX_LATENCY_US\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << exe << "\n";
    std::cout << "  " << exe << " --count 10000\n";
    std::cout << "  " << exe << " --server 127.0.0.1\n";
    std::cout << "  " << exe << " --server 192.168.0.207 --timeout none\n";
}

// Parse an integer argument in [min,max], returns false if invalid
static bool parse_int(const char *s, long min, long max, long &value) {
    char *end_ptr = nullptr;
    long parsed = std::strtol(s, &end_ptr, 10);
    if (end_ptr == s || *end_ptr != '\0' || parsed < min || parsed > max) {
        return false;
    }
    value = parsed;
    return true;
}

// Returns 0 on success, 1 on invalid arguments, -1 if help was printed
static int parse_args(int argc, char **argv, Options &opt) {
    const char *exe = (argc > 0 && argv[0] != nullptr) ? argv[0] : "xcp_test";

    const char *env = std::getenv("XCP_TEST_MAX_LATENCY_US");
    if (env != nullptr && env[0] != '\0') {
        long v = 0;
        if (!parse_int(env, 0, 10000000L, v)) {
            std::cerr << "Invalid XCP_TEST_MAX_LATENCY_US '" << env << "'\n";
            return 1;
        }
        opt.max_latency_us = static_cast<int>(v);
    }

    for (int i = 1; i < argc; i++) {
        const char *arg = argv[i];
        if (std::strcmp(arg, "-h") == 0 || std::strcmp(arg, "--help") == 0) {
            print_usage(exe);
            return -1;
        }
        if (i + 1 >= argc) {
            std::cerr << "Missing value or unknown option '" << arg << "'\n";
            print_usage(exe);
            return 1;
        }
        const char *val = argv[++i];
        long v = 0;
        if (std::strcmp(arg, "--server") == 0) {
            opt.server_ip = val;
        } else if (std::strcmp(arg, "--port") == 0) {
            if (!parse_int(val, 1, 65535, v)) {
                std::cerr << "Invalid port '" << val << "'\n";
                return 1;
            }
            opt.port = static_cast<uint16_t>(v);
        } else if (std::strcmp(arg, "--timeout") == 0) {
            if (std::strcmp(val, "none") == 0) {
                opt.timeout_enabled = false;
            } else {
                if (!parse_int(val, 0, 2147483647L, v)) {
                    std::cerr << "Invalid timeout '" << val << "'. Use milliseconds or 'none'.\n";
                    return 1;
                }
                opt.timeout_ms = static_cast<int>(v);
                opt.timeout_enabled = (opt.timeout_ms > 0);
            }
        } else if (std::strcmp(arg, "--count") == 0) {
            if (!parse_int(val, 0, 1000000L, v)) {
                std::cerr << "Invalid count '" << val << "'. Use 0..1000000.\n";
                return 1;
            }
            opt.latency_count = static_cast<int>(v);
        } else if (std::strcmp(arg, "--max-latency") == 0) {
            if (!parse_int(val, 0, 10000000L, v)) {
                std::cerr << "Invalid max latency '" << val << "'\n";
                return 1;
            }
            opt.max_latency_us = static_cast<int>(v);
        } else {
            std::cerr << "Unknown option '" << arg << "'\n";
            print_usage(exe);
            return 1;
        }
    }
    return 0;
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

// Check that the transport layer counter increments by 1 with each message received
static void check_counter(Client &c, uint16_t ctr, bool verbose) {
    if (c.have_prev_rx_ctr) {
        const uint16_t expected = static_cast<uint16_t>(c.prev_rx_ctr + 1);
        if (ctr != expected) {
            fail("Unexpected transport layer counter (prev=" + std::to_string(c.prev_rx_ctr) + ", current=" + std::to_string(ctr) + ", expected=" + std::to_string(expected) + ")");
        } else if (verbose) {
            std::cout << "Counter check: OK (" << c.prev_rx_ctr << " -> " << ctr << ")\n";
        }
    } else if (verbose) {
        std::cout << "Counter check: first response counter = " << ctr << "\n";
    }
    c.prev_rx_ctr = ctr;
    c.have_prev_rx_ctr = true;
}

// Send a command and receive its response (PID 0xFF or 0xFE)
// EVENT and SERV messages are skipped, multiple messages in one UDP datagram are supported
// verbose prints the raw bytes and the transport layer header, the latency measurement runs without any output
// Returns false on socket errors and timeouts, the caller checks the PID
static bool send_command(Client &c, const std::vector<uint8_t> &cmd, RxMessage &rx, bool verbose) {
    const auto pkt = build_xcp_packet(c.tx_ctr, cmd);
    if (verbose) {
        std::cout << "TX raw bytes: ";
        print_hex(pkt.data(), static_cast<int>(pkt.size()));
    }
    c.tx_ctr = static_cast<uint16_t>(c.tx_ctr + 1);

    int sent = static_cast<int>(
        sendto(c.sock, reinterpret_cast<const char *>(pkt.data()), static_cast<int>(pkt.size()), 0, reinterpret_cast<const sockaddr *>(&c.server), sizeof(c.server)));
    if (sent != static_cast<int>(pkt.size())) {
        fail("sendto() failed (sent=" + std::to_string(sent) + ")");
        return false;
    }

    for (;;) {
        uint8_t buf[512];
        int received = static_cast<int>(recvfrom(c.sock, reinterpret_cast<char *>(buf), sizeof(buf), 0, nullptr, nullptr));
        if (received <= 0) {
            fail("recvfrom() failed or timeout, no response");
            return false;
        }
        if (verbose) {
            std::cout << "RX raw bytes (" << received << " bytes): ";
            print_hex(buf, received);
        }

        // Iterate the messages in the datagram
        bool have_response = false;
        int i = 0;
        while (i < received) {
            if (received - i < 5) {
                fail("Datagram too short for a transport layer header and payload (" + std::to_string(received - i) + " bytes at offset " + std::to_string(i) + ")");
                break;
            }
            const uint16_t len = static_cast<uint16_t>(buf[i]) | (static_cast<uint16_t>(buf[i + 1]) << 8);
            const uint16_t ctr = static_cast<uint16_t>(buf[i + 2]) | (static_cast<uint16_t>(buf[i + 3]) << 8);
            if (len == 0) {
                if (verbose)
                    std::cout << "Ignoring " << (received - i) << " fill bytes at offset " << i << "\n";
                break;
            }
            if (len > received - i - 4) {
                fail("Transport layer length " + std::to_string(len) + " exceeds the datagram (" + std::to_string(received - i - 4) + " bytes available)");
                break;
            }
            if (verbose) {
                std::cout << "Transport layer header: len=" << len << ", ctr=" << ctr << "\n";
            }
            check_counter(c, ctr, verbose);

            const uint8_t pid = buf[i + 4];
            if (pid == PID_RES || pid == PID_ERR) {
                if (have_response) {
                    fail("More than one response in one datagram");
                } else {
                    rx.tl_len = len;
                    rx.tl_ctr = ctr;
                    rx.payload.assign(buf + i + 4, buf + i + 4 + len);
                    have_response = true;
                }
            } else if (pid == PID_EV || pid == PID_SERV) {
                if (verbose)
                    std::cout << "Skipping " << (pid == PID_EV ? "EVENT" : "SERV") << " message\n";
            } else {
                std::ostringstream m;
                m << "Unexpected PID 0x" << std::hex << std::uppercase << static_cast<int>(pid) << " (DAQ data without DAQ running?)";
                fail(m.str());
            }
            i += 4 + len;
        }
        if (have_response)
            return true;
    }
}

// Check for a positive response with at least min_len bytes
static bool check_response(const char *name, const RxMessage &rx, size_t min_len) {
    if (rx.payload.empty()) {
        fail(std::string(name) + ": empty response");
        return false;
    }
    if (rx.payload[0] == PID_ERR) {
        std::ostringstream s;
        s << name << ": negative response, error code 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
          << static_cast<int>(rx.payload.size() >= 2 ? rx.payload[1] : 0);
        fail(s.str());
        return false;
    }
    if (rx.payload.size() < min_len) {
        fail(std::string(name) + ": response too short (" + std::to_string(rx.payload.size()) + " bytes, expected " + std::to_string(min_len) + ")");
        return false;
    }
    return true;
}

// Print latency statistics, values in ns, returns the median
static int64_t print_latency_statistics(const char *name, std::vector<int64_t> &samples) {
    if (samples.empty())
        return 0;
    std::sort(samples.begin(), samples.end());
    int64_t sum = 0;
    for (int64_t s : samples)
        sum += s;
    auto us = [](double ns) { return ns / 1000.0; };
    auto percentile = [&](double p) { return samples[std::min(samples.size() - 1, static_cast<size_t>(p * static_cast<double>(samples.size())))]; };
    std::cout << std::fixed << std::setprecision(1);
    std::cout << name << " (" << samples.size() << " samples):\n";
    std::cout << "  min    : " << us(static_cast<double>(samples.front())) << " us\n";
    std::cout << "  median : " << us(static_cast<double>(percentile(0.5))) << " us\n";
    std::cout << "  avg    : " << us(static_cast<double>(sum) / static_cast<double>(samples.size())) << " us\n";
    std::cout << "  p90    : " << us(static_cast<double>(percentile(0.9))) << " us\n";
    std::cout << "  p99    : " << us(static_cast<double>(percentile(0.99))) << " us\n";
    std::cout << "  max    : " << us(static_cast<double>(samples.back())) << " us\n";
    std::cout.unsetf(std::ios::floatfield);
    std::cout << std::setprecision(6);
    return percentile(0.5);
}

// Decode and print the CONNECT response payload
static void decode_connect_response(const std::vector<uint8_t> &payload) {
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

    std::cout << "  resources                : 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(resource) << "  (" << res_str << ")\n";
    std::cout << "  comm_mode_basic          : 0x" << std::setw(2) << static_cast<int>(comm_mode) << "  (byte_order=" << byte_order << ", addr_gran=" << gran_str << ")\n";
    std::cout << "  max_cto_size             : " << std::dec << static_cast<int>(max_cto) << " bytes\n";
    std::cout << "  max_dto_size             : " << max_dto << " bytes\n";
    std::cout << "  protocol_version         : 0x" << std::hex << std::setw(2) << static_cast<int>(proto_ver) << '\n';
    std::cout << "  transport_layer_version  : 0x" << std::setw(2) << static_cast<int>(tl_ver) << '\n';
    std::cout << std::dec;
}

static void decode_get_version_response(const std::vector<uint8_t> &payload) {
    const uint16_t protocol_version = (static_cast<uint16_t>(payload[2]) << 8) | payload[3];
    const uint16_t transport_layer_version = (static_cast<uint16_t>(payload[4]) << 8) | payload[5];
    std::cout << "  protocol_version         : 0x" << std::hex << std::uppercase << std::setw(4) << std::setfill('0') << protocol_version << '\n';
    std::cout << "  transport_layer_version  : 0x" << std::setw(4) << transport_layer_version << '\n';
    std::cout << std::dec;
}

static void decode_get_comm_mode_info_response(const std::vector<uint8_t> &payload) {
    const uint8_t comm_mode_optional = payload[2];
    const uint8_t max_bs = payload[3];
    const uint8_t min_st = payload[4];
    const uint8_t queue_size = payload[5];
    const uint8_t xcp_driver_version_number = payload[7];

    std::cout << "  comm_mode_optional       : 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(comm_mode_optional) << '\n';
    std::cout << "  max_bs                   : " << std::dec << static_cast<int>(max_bs) << '\n';
    std::cout << "  min_st                   : " << static_cast<int>(min_st) << '\n';
    std::cout << "  queue_size               : " << static_cast<int>(queue_size) << '\n';
    std::cout << "  driver_version           : 0x" << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << static_cast<int>(xcp_driver_version_number) << '\n';
    std::cout << std::dec;
}

// Run the test sequence, errors are counted in gErrors
// Returns false if the sequence had to be aborted
static bool run_test(Client &c, const Options &opt) {

    // CONNECT
    RxMessage rx;
    std::cout << "\nSending XCP CONNECT\n";
    if (!send_command(c, {CC_CONNECT, 0x00}, rx, true))
        return false;
    if (!check_response("CONNECT", rx, 8))
        return false;
    std::cout << "XCP CONNECT response decoded:\n";
    decode_connect_response(rx.payload);
    const uint8_t comm_mode_basic = rx.payload[2];

    // GET_VERSION
    std::cout << "\nSending XCP GET_VERSION\n";
    if (!send_command(c, {CC_GET_VERSION, 0x00}, rx, true))
        return false;
    if (check_response("GET_VERSION", rx, 6)) {
        std::cout << "XCP GET_VERSION response decoded:\n";
        decode_get_version_response(rx.payload);
    }

    // GET_COMM_MODE_INFO, if supported
    if ((comm_mode_basic & 0x80) != 0) {
        std::cout << "\nSending XCP GET_COMM_MODE_INFO\n";
        if (!send_command(c, {CC_GET_COMM_MODE_INFO, 0x00}, rx, true))
            return false;
        if (check_response("GET_COMM_MODE_INFO", rx, 8)) {
            std::cout << "XCP GET_COMM_MODE_INFO response decoded:\n";
            decode_get_comm_mode_info_response(rx.payload);
        }
    } else {
        std::cout << "\nSkipping GET_COMM_MODE_INFO because CONNECT response indicates it is not supported (comm_mode_basic bit7 not set).\n";
    }

    // Command round trip latency measurement with GET_STATUS
    // Back to back: steady state latency, all threads involved are awake
    // After idle: includes the wake-up latency of the client and server threads, depends on OS power management, only reported
    if (opt.latency_count > 0) {
        std::cout << "\nLatency measurement with GET_STATUS:\n";
        std::vector<int64_t> samples;
        samples.reserve(static_cast<size_t>(opt.latency_count));
        for (int i = 0; i < LATENCY_WARMUP_COUNT + opt.latency_count; i++) {
            const auto t0 = std::chrono::steady_clock::now();
            if (!send_command(c, {CC_GET_STATUS}, rx, false))
                return false;
            const auto t1 = std::chrono::steady_clock::now();
            if (!check_response("GET_STATUS", rx, 6))
                return false;
            if (i >= LATENCY_WARMUP_COUNT)
                samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        }
        const int64_t median_ns = print_latency_statistics("Back to back", samples);

        samples.clear();
        for (int i = 0; i < LATENCY_IDLE_COUNT; i++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(LATENCY_IDLE_MS));
            const auto t0 = std::chrono::steady_clock::now();
            if (!send_command(c, {CC_GET_STATUS}, rx, false))
                return false;
            const auto t1 = std::chrono::steady_clock::now();
            if (!check_response("GET_STATUS", rx, 6))
                return false;
            samples.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        }
        std::string name = "After " + std::to_string(LATENCY_IDLE_MS) + " ms idle";
        print_latency_statistics(name.c_str(), samples);

        if (opt.max_latency_us > 0) {
            if (median_ns > static_cast<int64_t>(opt.max_latency_us) * 1000) {
                fail("Median back to back latency " + std::to_string(median_ns / 1000) + " us exceeds the limit of " + std::to_string(opt.max_latency_us) + " us");
            } else {
                std::cout << "Median back to back latency within the limit of " << opt.max_latency_us << " us\n";
            }
        }
    }

    // DISCONNECT
    std::cout << "\nSending XCP DISCONNECT\n";
    if (!send_command(c, {CC_DISCONNECT}, rx, true))
        return false;
    check_response("DISCONNECT", rx, 1);
    return true;
}

// Check that a UDP port on 127.0.0.1 is free, by binding a socket to it
static bool udp_port_free(uint16_t port) {
    socket_t s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == SOCKET_INVALID)
        return false;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    a.sin_port = htons(port);
    bool ok = bind(s, reinterpret_cast<sockaddr *>(&a), sizeof(a)) == 0;
    CLOSE_SOCKET(s);
    return ok;
}

// Start the in-process XCP server on 127.0.0.1, on the first free port from LOCAL_SERVER_PORT
// Returns the port, 0 on failure
static uint16_t start_local_server() {
    uint16_t port = 0;
    for (int i = 0; i < LOCAL_SERVER_PORT_TRIES; i++) {
        if (udp_port_free(static_cast<uint16_t>(LOCAL_SERVER_PORT + i))) {
            port = static_cast<uint16_t>(LOCAL_SERVER_PORT + i);
            break;
        }
    }
    if (port == 0) {
        std::cerr << "No free UDP port in " << LOCAL_SERVER_PORT << ".." << (LOCAL_SERVER_PORT + LOCAL_SERVER_PORT_TRIES - 1) << "\n";
        return 0;
    }

    XcpSetLogLevel(LOCAL_SERVER_LOG_LEVEL);
    XcpInit("xcp_test", "V1.0", XCP_MODE_LOCAL);
    const uint8_t addr[4] = {127, 0, 0, 1};
    if (!XcpEthServerInit(addr, port, false, LOCAL_SERVER_QUEUE_SIZE)) {
        std::cerr << "Failed to start the in-process XCP server on port " << port << "\n";
        return 0;
    }
    return port;
}

int main(int argc, char **argv) {
    Options opt;
    int r = parse_args(argc, argv, opt);
    if (r != 0)
        return r < 0 ? 0 : 1;

#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    // Start the in-process server, unless an external server is given
    const bool local_server = (opt.server_ip == nullptr);
    if (local_server) {
        uint16_t port = start_local_server();
        if (port == 0)
            return 1;
        opt.server_ip = "127.0.0.1";
        opt.port = port;
        std::cout << "Mode: hermetic self-check, in-process XCP on UDP server on 127.0.0.1:" << port << " (pass --server <ip> to probe an external server)\n";
    } else {
        std::cout << "Mode: probe external XCP on UDP server " << opt.server_ip << ":" << opt.port << "\n";
    }

    Client c;

    // Create UDP socket
    c.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (c.sock == SOCKET_INVALID) {
        std::cerr << "socket() failed\n";
        return 1;
    }

    // Bind to any local address (OS picks a port)
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0;
    if (bind(c.sock, reinterpret_cast<sockaddr *>(&local), sizeof(local)) != 0) {
        std::cerr << "bind() failed\n";
        CLOSE_SOCKET(c.sock);
        return 1;
    }

    // Print the actual UDP source port selected for the client socket.
    sockaddr_in local_bound{};
#ifdef _WIN32
    int local_bound_len = sizeof(local_bound);
#else
    socklen_t local_bound_len = sizeof(local_bound);
#endif
    if (getsockname(c.sock, reinterpret_cast<sockaddr *>(&local_bound), &local_bound_len) == 0) {
        std::cout << "Client UDP source port: " << ntohs(local_bound.sin_port) << '\n';
    } else {
        std::cout << "Client UDP source port: unknown (getsockname failed)\n";
    }

    // Set receive timeout (optional)
#ifdef _WIN32
    DWORD timeout_opt = opt.timeout_enabled ? static_cast<DWORD>(opt.timeout_ms) : 0;
    setsockopt(c.sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout_opt), sizeof(timeout_opt));
#else
    timeval tv{};
    if (opt.timeout_enabled) {
        tv.tv_sec = opt.timeout_ms / 1000;
        tv.tv_usec = (opt.timeout_ms % 1000) * 1000;
    }
    setsockopt(c.sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    if (opt.timeout_enabled) {
        std::cout << "Receive timeout: " << opt.timeout_ms << " ms\n";
    } else {
        std::cout << "Receive timeout: disabled\n";
    }

    c.server.sin_family = AF_INET;
    c.server.sin_port = htons(opt.port);
    if (inet_pton(AF_INET, opt.server_ip, &c.server.sin_addr) != 1) {
        std::cerr << "inet_pton() failed for " << opt.server_ip << '\n';
        CLOSE_SOCKET(c.sock);
        return 1;
    }

    if (!run_test(c, opt)) {
        std::cerr << "Test sequence aborted\n";
    }

    CLOSE_SOCKET(c.sock);
    if (local_server) {
        XcpDisconnect();
        XcpEthServerShutdown();
    }
#ifdef _WIN32
    WSACleanup();
#endif

    if (gErrors > 0) {
        std::cout << "\nFAILED: " << gErrors << " error(s)\n";
        return 1;
    }
    std::cout << "\nPASSED\n";
    return 0;
}
