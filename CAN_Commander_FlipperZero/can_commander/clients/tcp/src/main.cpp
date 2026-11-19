#include <algorithm>
#include <atomic>
#include <cerrno>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#ifndef _WIN32
#include <csignal>
#endif
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <conio.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <io.h>

using SocketHandle = SOCKET;
static constexpr SocketHandle InvalidSocket = INVALID_SOCKET;
#else
#include <netdb.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

using SocketHandle = int;
static constexpr SocketHandle InvalidSocket = -1;
#endif

namespace {

void close_socket(SocketHandle socket) {
#ifdef _WIN32
    closesocket(socket);
#else
    close(socket);
#endif
}

void shutdown_send(SocketHandle socket) {
#ifdef _WIN32
    shutdown(socket, SD_SEND);
#else
    shutdown(socket, SHUT_WR);
#endif
}

void shutdown_both(SocketHandle socket) {
#ifdef _WIN32
    shutdown(socket, SD_BOTH);
#else
    shutdown(socket, SHUT_RDWR);
#endif
}

bool send_all(SocketHandle socket, const char* data, std::size_t size) {
    while(size > 0) {
#ifdef _WIN32
        const int sent = send(socket, data, static_cast<int>(size), 0);
#else
        const ssize_t sent = send(socket, data, size, 0);
#endif
        if(sent < 0) {
#ifdef _WIN32
            if(WSAGetLastError() == WSAEINTR) continue;
#else
            if(errno == EINTR) continue;
#endif
            return false;
        }
        if(sent == 0) return false;
        data += sent;
        size -= static_cast<std::size_t>(sent);
    }
    return true;
}

SocketHandle connect_to_server(const char* host, const char* port) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* addresses = nullptr;
    const int lookup_result = getaddrinfo(host, port, &hints, &addresses);
    if(lookup_result != 0) {
#ifdef _WIN32
        std::cerr << "Could not resolve server: " << lookup_result << '\n';
#else
        std::cerr << "Could not resolve server: " << gai_strerror(lookup_result) << '\n';
#endif
        return InvalidSocket;
    }

    SocketHandle connected = InvalidSocket;
    for(addrinfo* address = addresses; address; address = address->ai_next) {
        SocketHandle candidate = socket(
            address->ai_family, address->ai_socktype, address->ai_protocol);
        if(candidate == InvalidSocket) continue;

#ifdef _WIN32
        const int result = connect(
            candidate, address->ai_addr, static_cast<int>(address->ai_addrlen));
#else
        const int result = connect(candidate, address->ai_addr, address->ai_addrlen);
#endif
        if(result == 0) {
            connected = candidate;
            break;
        }
        close_socket(candidate);
    }
    freeaddrinfo(addresses);
    return connected;
}

#ifdef _WIN32
bool read_console_input(SocketHandle socket, std::string& line, bool& input_closed) {
    while(_kbhit()) {
        const int key = _getch();
        if(key == 0 || key == 0xE0) {
            _getch();
            continue;
        }
        if(key == 26) {
            input_closed = true;
            shutdown_send(socket);
            return true;
        }
        if(key == '\r') {
            std::cout << '\n' << std::flush;
            line.push_back('\n');
            if(!send_all(socket, line.data(), line.size())) return false;
            line.clear();
        } else if(key == '\b') {
            if(!line.empty()) {
                line.pop_back();
                std::cout << "\b \b" << std::flush;
            }
        } else if(key >= 0x20) {
            line.push_back(static_cast<char>(key));
            std::cout.put(static_cast<char>(key)).flush();
        }
    }
    return true;
}
#endif

struct CanFrameLine {
    std::string bus;
    std::string id;
    bool extended = false;
    bool remote = false;
    unsigned int dlc = 0;
    std::vector<std::string> data;
};

bool parse_hex_byte(const std::string& text) {
    if(text.size() != 2) return false;
    unsigned int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size() && value <= 0xFF;
}

bool parse_can_frame_line(const std::string& line, CanFrameLine& frame) {
    std::istringstream input(line);
    std::vector<std::string> tokens;
    std::string token;
    while(input >> token) tokens.push_back(token);
    if(tokens.size() < 6 || tokens[0].rfind("bus=", 0) != 0 ||
       tokens[1].rfind("id=0x", 0) != 0 || tokens[4].rfind("dlc=", 0) != 0 ||
       tokens[5].rfind("data=", 0) != 0) {
        return false;
    }

    frame.bus = tokens[0].substr(4);
    frame.id = tokens[1].substr(3);
    if(frame.bus != "can0" && frame.bus != "can1") return false;
    if(tokens[2] != "std" && tokens[2] != "ext") return false;
    if(tokens[3] != "data" && tokens[3] != "rtr") return false;
    frame.extended = tokens[2] == "ext";
    frame.remote = tokens[3] == "rtr";

    const std::string dlc_text = tokens[4].substr(4);
    const auto dlc_result = std::from_chars(
        dlc_text.data(), dlc_text.data() + dlc_text.size(), frame.dlc, 10);
    if(dlc_result.ec != std::errc{} || dlc_result.ptr != dlc_text.data() + dlc_text.size() ||
       frame.dlc > 8) {
        return false;
    }

    frame.data.clear();
    const std::string first_byte = tokens[5].substr(5);
    if(!first_byte.empty()) {
        if(!parse_hex_byte(first_byte)) return false;
        frame.data.push_back(first_byte);
    }
    for(std::size_t i = 6; i < tokens.size() && frame.data.size() < frame.dlc; ++i) {
        if(!parse_hex_byte(tokens[i])) return false;
        frame.data.push_back(tokens[i]);
    }
    return frame.data.size() == frame.dlc;
}

std::string json_escape(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string escaped;
    for(unsigned char c : value) {
        switch(c) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if(c < 0x20) {
                escaped += "\\u00";
                escaped += hex[(c >> 4) & 0x0F];
                escaped += hex[c & 0x0F];
            } else {
                escaped += static_cast<char>(c);
            }
        }
    }
    return escaped;
}

using CaptureClock = std::chrono::steady_clock;

struct CaptureState {
    std::ofstream output;
    CaptureClock::time_point started = CaptureClock::now();
    std::mutex mutex;
    std::condition_variable changed;
    std::mutex console_mutex;
    std::atomic<bool> stop_reader{false};
    std::atomic<unsigned long long> frame_count{0};
    bool greeting_received = false;
    bool awaiting_status = false;
    bool status_received = false;
    std::string tool_status;
    bool awaiting_start = false;
    bool start_acknowledged = false;
    bool start_failed = false;
    std::string start_error;
    bool stop_acknowledged = false;
    bool reader_done = false;
    bool output_failed = false;
    bool output_error_reported = false;
};

unsigned long long capture_timestamp_ms(const CaptureState& state) {
    return static_cast<unsigned long long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            CaptureClock::now() - state.started)
            .count());
}

bool write_json_record(CaptureState& state, const std::string& record, bool flush) {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.output << record << '\n';
    if(flush) state.output.flush();
    if(!state.output) state.output_failed = true;
    return !state.output_failed;
}

bool write_session_start(CaptureState& state, const char* host, const char* port) {
    return write_json_record(
        state,
        "{\"type\":\"session_start\",\"timestamp_ms\":0,\"host\":\"" +
            json_escape(host) + "\",\"port\":\"" + json_escape(port) +
            "\",\"mode\":\"passive_read_all\",\"bus\":\"can0\"}",
        true);
}

bool write_action_record(CaptureState& state, const std::string& action) {
    const auto timestamp = capture_timestamp_ms(state);
    return write_json_record(
        state,
        "{\"type\":\"action\",\"timestamp_ms\":" + std::to_string(timestamp) +
            ",\"label\":\"" + json_escape(action) + "\"}",
        true);
}

bool write_frame_record(CaptureState& state, const CanFrameLine& frame) {
    const auto timestamp = capture_timestamp_ms(state);
    std::ostringstream record;
    record << "{\"type\":\"can_frame\",\"timestamp_ms\":" << timestamp
           << ",\"bus\":\"" << json_escape(frame.bus)
           << "\",\"id\":\"" << json_escape(frame.id)
           << "\",\"extended\":" << (frame.extended ? "true" : "false")
           << ",\"remote\":" << (frame.remote ? "true" : "false")
           << ",\"dlc\":" << frame.dlc << ",\"data\":[";
    for(std::size_t i = 0; i < frame.data.size(); ++i) {
        if(i > 0) record << ',';
        record << '"' << frame.data[i] << '"';
    }
    record << "]}";
    const bool written = write_json_record(state, record.str(), false);
    if(written) ++state.frame_count;
    return written;
}

bool write_session_end(CaptureState& state) {
    const auto timestamp = capture_timestamp_ms(state);
    const auto frames = state.frame_count.load();
    return write_json_record(
        state,
        "{\"type\":\"session_end\",\"timestamp_ms\":" + std::to_string(timestamp) +
            ",\"frames\":" + std::to_string(frames) + "}",
        true);
}

bool is_tool_description(const std::string& line) {
    if(line == "no active tool") return true;
    static const char* names[] = {
        "read_all ", "filtered ", "write ", "speed ", "valtrack ", "unique_ids ",
        "bittrack ", "reverse ", "obd_pid ", "dbc_decode ", "custom_inject ", "replay "};
    for(const char* name : names) {
        if(line.rfind(name, 0) == 0) return true;
    }
    return false;
}

void process_capture_line(CaptureState& state, const std::string& line) {
    CanFrameLine frame;
    if(parse_can_frame_line(line, frame)) {
        const bool recorded = write_frame_record(state, frame);
        if(!recorded) {
            bool report_error = false;
            {
                std::lock_guard<std::mutex> lock(state.mutex);
                if(!state.output_error_reported) {
                    state.output_error_reported = true;
                    report_error = true;
                }
            }
            if(report_error) {
                std::lock_guard<std::mutex> console_lock(state.console_mutex);
                std::cerr << "Could not write CAN frame to capture file\n";
            }
        }
        const auto count = state.frame_count.load();
        if(recorded && count > 0 && count % 100 == 0) {
            std::lock_guard<std::mutex> console_lock(state.console_mutex);
            std::cout << "[captured " << count << " CAN frames]\n" << std::flush;
        }
    } else {
        std::lock_guard<std::mutex> console_lock(state.console_mutex);
        std::cout << line << '\n' << std::flush;
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        if(line.rfind("WiFi CLI ready.", 0) == 0) state.greeting_received = true;
        if(state.awaiting_status && is_tool_description(line)) {
            state.tool_status = line;
            state.status_received = true;
        }
        if(state.awaiting_start && line == "tool started: read_all") {
            state.start_acknowledged = true;
            state.awaiting_start = false;
        } else if(state.awaiting_start && line.rfind("ERR:", 0) == 0) {
            state.start_failed = true;
            state.start_error = line;
            state.awaiting_start = false;
        }
        if(line == "tool stopped") state.stop_acknowledged = true;
    }
    state.changed.notify_all();
}

void capture_reader(SocketHandle socket, CaptureState& state) {
    char buffer[4096];
    std::string line;
    bool discarding_line = false;

    while(!state.stop_reader.load()) {
#ifdef _WIN32
        const int received = recv(socket, buffer, static_cast<int>(sizeof(buffer)), 0);
#else
        const ssize_t received = recv(socket, buffer, sizeof(buffer), 0);
#endif
        if(received == 0) break;
        if(received < 0) {
#ifdef _WIN32
            const int error = WSAGetLastError();
            if(error == WSAEINTR) continue;
#else
            const int error = errno;
            if(error == EINTR) continue;
#endif
            if(!state.stop_reader.load()) {
                std::lock_guard<std::mutex> console_lock(state.console_mutex);
                std::cerr << "Could not receive from server during capture\n";
            }
            break;
        }

        for(int i = 0; i < received; ++i) {
            const char c = buffer[i];
            if(c == '\n') {
                if(!discarding_line) {
                    if(!line.empty() && line.back() == '\r') line.pop_back();
                    process_capture_line(state, line);
                }
                line.clear();
                discarding_line = false;
            } else if(!discarding_line) {
                if(line.size() < 4096) {
                    line.push_back(c);
                } else {
                    line.clear();
                    discarding_line = true;
                }
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.reader_done = true;
    }
    state.changed.notify_all();
}

bool send_capture_command(SocketHandle socket, const char* command) {
    return send_all(socket, command, std::strlen(command));
}

int run_capture(SocketHandle socket, const char* host, const char* port, const char* path) {
    CaptureState state;
    state.output.open(path, std::ios::out | std::ios::app);
    if(!state.output) {
        std::cerr << "Could not open capture file: " << path << '\n';
        return 1;
    }
    if(!write_session_start(state, host, port)) {
        std::cerr << "Could not write capture file: " << path << '\n';
        return 1;
    }

    std::thread reader(capture_reader, socket, std::ref(state));
    const auto finish = [&]() {
        state.stop_reader = true;
        shutdown_both(socket);
        if(reader.joinable()) reader.join();
        if(!write_session_end(state)) {
            std::cerr << "Could not finish capture file: " << path << '\n';
        }
    };

    bool greeting_received = false;
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.changed.wait_for(lock, std::chrono::seconds(3), [&]() {
            return state.greeting_received || state.reader_done;
        });
        greeting_received = state.greeting_received;
        if(greeting_received) state.awaiting_status = true;
    }
    if(!greeting_received) {
        std::cerr << "Server did not send the CAN Commander CLI greeting\n";
        finish();
        return 1;
    }
    if(!send_capture_command(socket, "tool status\n")) {
        std::cerr << "Could not query active tool status\n";
        finish();
        return 1;
    }

    std::string tool_status;
    bool status_received = false;
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.changed.wait_for(lock, std::chrono::seconds(3), [&]() {
            return state.status_received || state.reader_done;
        });
        status_received = state.status_received;
        if(status_received) tool_status = state.tool_status;
    }
    if(!status_received) {
        std::cerr << "Did not receive active tool status\n";
        finish();
        return 1;
    }
    if(tool_status != "no active tool") {
        std::cerr << "Refusing to replace the running tool: " << tool_status << '\n';
        finish();
        return 1;
    }

    {
        std::lock_guard<std::mutex> lock(state.mutex);
        state.awaiting_start = true;
    }
    if(!send_capture_command(socket, "tool start read_all bus=can0 ascii=0\n")) {
        std::cerr << "Could not start passive CAN capture\n";
        finish();
        return 1;
    }
    bool start_acknowledged = false;
    std::string start_error;
    {
        std::unique_lock<std::mutex> lock(state.mutex);
        state.changed.wait_for(lock, std::chrono::seconds(3), [&]() {
            return state.start_acknowledged || state.start_failed || state.reader_done;
        });
        start_acknowledged = state.start_acknowledged;
        start_error = state.start_error;
    }
    if(!start_acknowledged) {
        std::cerr << (start_error.empty() ? "Passive CAN capture did not start" : start_error)
                  << '\n';
        finish();
        return 1;
    }

    std::cout << "Passive CAN0 capture is running. It sends no CAN frames.\n"
              << "Use a bench or stationary vehicle secured in Park with the parking brake set; never do this while driving.\n"
              << "Enter an action label and press Enter to timestamp the cue; perform the action immediately after.\n"
              << "Example: baseline idle, press accelerator slowly, release accelerator. Type /done to stop.\n";
    std::string action;
    while(true) {
        {
            std::lock_guard<std::mutex> console_lock(state.console_mutex);
            std::cout << "Action cue: " << std::flush;
        }
        if(!std::getline(std::cin, action)) break;
        const auto first = action.find_first_not_of(" \t\r\n");
        if(first == std::string::npos) continue;
        const auto last = action.find_last_not_of(" \t\r\n");
        action = action.substr(first, last - first + 1);
        if(action == "/done") break;
        if(!write_action_record(state, action)) {
            std::cerr << "Could not write action annotation to capture file\n";
            break;
        }
        {
            std::lock_guard<std::mutex> console_lock(state.console_mutex);
            std::cout << "[action cue recorded at +" << capture_timestamp_ms(state)
                      << " ms; perform it now]\n";
        }
    }

    if(!send_capture_command(socket, "tool stop\n")) {
        std::cerr << "Could not stop passive CAN capture cleanly\n";
    } else {
        std::unique_lock<std::mutex> lock(state.mutex);
        if(!state.changed.wait_for(lock, std::chrono::seconds(2), [&]() {
               return state.stop_acknowledged || state.reader_done;
           }) ||
           !state.stop_acknowledged) {
            std::cerr << "Warning: did not receive tool-stop acknowledgement\n";
        }
    }

    finish();
    return state.output_failed ? 1 : 0;
}

} // namespace

int main(int argc, char** argv) {
    bool capture_mode = false;
    const char* host = nullptr;
    const char* port = "7777";
    const char* capture_path = nullptr;
    if(argc >= 2 && std::strcmp(argv[1], "capture") == 0) {
        if(argc < 4 || argc > 5) {
            std::cerr << "Usage: cancommander-tcp capture <host> <output.jsonl> [port]\n";
            return 2;
        }
        capture_mode = true;
        host = argv[2];
        capture_path = argv[3];
        if(argc == 5) port = argv[4];
    } else {
        if(argc < 2 || argc > 3) {
            std::cerr << "Usage: cancommander-tcp <host> [port]\n"
                         "       cancommander-tcp capture <host> <output.jsonl> [port]\n";
            return 2;
        }
        host = argv[1];
        if(argc == 3) port = argv[2];
    }

#ifdef _WIN32
    WSADATA winsock_data{};
    if(WSAStartup(MAKEWORD(2, 2), &winsock_data) != 0) {
        std::cerr << "Could not initialize Winsock\n";
        return 1;
    }
    if(_isatty(_fileno(stdin)) == 0) {
        std::cerr << "Interactive console input is required on Windows\n";
        WSACleanup();
        return 2;
    }
#else
    std::signal(SIGPIPE, SIG_IGN);
#endif

    const SocketHandle socket = connect_to_server(host, port);
    if(socket == InvalidSocket) {
        std::cerr << "Could not connect to " << host << ':' << port << '\n';
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    if(capture_mode) {
        const int result = run_capture(socket, host, port, capture_path);
        close_socket(socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return result;
    }

    std::cerr << "Connected to " << host << ':' << port
              << ". Enter CLI commands; close stdin to disconnect.\n";
    bool input_closed = false;
    std::string line;
    char buffer[4096];

    for(;;) {
        fd_set read_set;
        FD_ZERO(&read_set);
        FD_SET(socket, &read_set);
#ifndef _WIN32
        int max_fd = socket;
        if(!input_closed) {
            FD_SET(STDIN_FILENO, &read_set);
            max_fd = std::max(max_fd, STDIN_FILENO);
        }
#endif
        timeval timeout{};
        timeout.tv_usec = 100000;
#ifdef _WIN32
        const int ready = select(0, &read_set, nullptr, nullptr, &timeout);
#else
        const int ready = select(max_fd + 1, &read_set, nullptr, nullptr, &timeout);
#endif
        if(ready < 0) {
#ifdef _WIN32
            if(WSAGetLastError() == WSAEINTR) continue;
#else
            if(errno == EINTR) continue;
#endif
            std::cerr << "Socket polling failed\n";
            break;
        }

#ifndef _WIN32
        if(ready > 0 && FD_ISSET(STDIN_FILENO, &read_set) && !input_closed) {
            const ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
            if(count == 0) {
                input_closed = true;
                shutdown_send(socket);
            } else if(count < 0) {
                if(errno == EINTR) continue;
                std::cerr << "Could not read stdin: " << std::strerror(errno) << '\n';
                break;
            } else if(!send_all(socket, buffer, static_cast<std::size_t>(count))) {
                std::cerr << "Could not send command to server\n";
                break;
            }
        }
#else
        if(!input_closed && !read_console_input(socket, line, input_closed)) {
            std::cerr << "Could not send command to server\n";
            break;
        }
#endif

        if(ready > 0 && FD_ISSET(socket, &read_set)) {
#ifdef _WIN32
            const int count = recv(socket, buffer, static_cast<int>(sizeof(buffer)), 0);
#else
            const ssize_t count = recv(socket, buffer, sizeof(buffer), 0);
#endif
            if(count == 0) break;
            if(count < 0) {
#ifdef _WIN32
                if(WSAGetLastError() == WSAEINTR) continue;
#else
                if(errno == EINTR) continue;
#endif
                std::cerr << "Could not receive from server\n";
                break;
            }
            std::cout.write(buffer, count).flush();
        }
    }

    close_socket(socket);
#ifdef _WIN32
    WSACleanup();
#endif
    return 0;
}
