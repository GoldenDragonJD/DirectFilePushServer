#include <cstring>
#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string>
#include <arpa/inet.h>
#include <thread>
#include <atomic>
#include <csignal>
#include <format>
#include <vector>
#include <memory>
#include <chrono>
#include <sys/time.h>
#include "include/json.hpp"
#define IPADDRESS "0.0.0.0"

using json = nlohmann::json;

sockaddr_in server;
int server_socket = -1;
std::atomic<unsigned int> client_count = 0;
std::atomic running(true);

int MAX_SIZE = 4;
int CHUNK_SIZE = 1024*256;
constexpr int EMPTY    = -1;

std::vector<std::atomic<int>> slots;

std::string get_time()
{
    auto now_utc = std::chrono::system_clock::now();

    std::chrono::zoned_time local_time{std::chrono::current_zone(), now_utc};

    std::string formatted = std::format("{:%OH:%OM:%OS %m/%d/%y} ", local_time);

    return formatted;
}

void initSlots() {
    slots = std::vector<std::atomic<int>>(MAX_SIZE);
    for (auto &s : slots) {
        s.store(EMPTY);
    }
}

bool addNumber(int num) {
    for (auto &s : slots) {
        int expected = EMPTY;
        // try to claim an EMPTY slot
        if (s.compare_exchange_strong(expected, num)) {
            return true;
        }
    }
    return false; // full
}

bool removeNumber(int num) {
    for (auto &s : slots) {
        int current = s.load();
        if (current == num) {
            return s.compare_exchange_strong(current, EMPTY);
        }
    }
    return false; // not found
}

std::string receive_message(const int sock, std::string output = "")
{
    char c;

    std::cerr << get_time() << "Waiting to read from socket fd=" << sock << std::endl;

    while (true)
    {
        const ssize_t n = read(sock, &c, 1);

        if (n == 0) {
            std::cerr << get_time() << "Peer closed connection\n";
            break;
        }
        if (n < 0) {
            if (errno == EINTR) continue;
            std::cerr << get_time() << "Read error: " << strerror(errno) << std::endl;
            break;
        }

        output.push_back(c);

        if (c == '\n') {
            break;
        }
    }

    if (!output.empty() && output.back() == '\n') {
        output.pop_back();
    }

    std::cout << get_time() << "Received: " << output << std::endl;
    return output;
}

void write_message(const int sock, const std::string &json_message)
{
    std::string output = json_message;
    output = output + "\n";
    write(sock, output.c_str(), output.size());
}

void handle_client(int c)
{
    if (client_count > MAX_SIZE)
    {
        std::cout << get_time() << "Server full message sent" << std::endl;

        int id = -1;

        if (write(c, &id, sizeof(int)) != sizeof(int)) {
            std::cerr << get_time() << "Warning: failed to send id to client fd=" << c << std::endl;
        }

        close(c);
        removeNumber(c);
        client_count = client_count - 1;

        return;
    }

    std::cout << get_time() << "client " << c << " connected" << std::endl;

    // send client id as an int (so python client can unpack)
    int id = c;
    if (write(c, &id, sizeof(id)) != sizeof(id)) {
        std::cerr << get_time() << "Warning: failed to send id to client fd=" << c << std::endl;
    }

    char check_buffer[21];

    timeval timeout{};
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;

    if (setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt failed");
    }

    read(c, check_buffer, sizeof(check_buffer) - 1);

    timeout.tv_sec = 0;
    timeout.tv_usec = 0;
    setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    std::cout << get_time() << check_buffer << " received" << std::endl;

    if (std::string(check_buffer, 20) != "DirectFilePushClient")
    {
        close(c);
        removeNumber(c);
        client_count = client_count - 1;
        std::cout << get_time() << "Client fd=" << c << " handler exiting\n";
        return;
    }

    unsigned long long current_file_size = 0;
    unsigned long long total_file_size = 0;
    bool sending_file = false;
    int sending_target = -1;
    std::string line;
    std::byte buffer[CHUNK_SIZE];

    while (running)
    {
        try
        {
            if (sending_file) {
                unsigned long long remaining = total_file_size - current_file_size;
                unsigned long long toRead = std::min(static_cast<unsigned long long>(CHUNK_SIZE), remaining);

                unsigned int bytes_read = read(c, buffer, toRead);

                if (bytes_read == 0) {
                    std::cerr << get_time() << "Sender closed connection early at "
                              << current_file_size << " / " << total_file_size << std::endl;
                    sending_file = false;
                    continue;
                }

                current_file_size += bytes_read;

                if (current_file_size > total_file_size)
                {
                    bytes_read = current_file_size - total_file_size;
                }

                unsigned int bytes_written = write(sending_target, buffer, bytes_read);

                while (bytes_written < bytes_read)
                {
                    unsigned int remaining_to_write = bytes_read - bytes_written;
                    bytes_written = write(sending_target, buffer, remaining_to_write);
                }

                if (current_file_size >= total_file_size) {
                    std::cout << get_time() << "Finished forwarding file from fd " << c
                              << " to fd " << sending_target << std::endl;
                    sending_file = false;
                    current_file_size = 0;
                    total_file_size = 0;
                    sending_target = -1;
                }

                continue;
            }

            // read a JSON control line (newline-terminated)
            line = receive_message(c);
            if (line.empty()) {
                std::cerr << get_time() << "receive_message returned empty (peer closed?) for fd=" << c << std::endl;
                break;
            }

            json message = json::parse(line);

            const std::string type = message.value("type", "");

            if (type == "request")
            {
                int target_id = message.value("target", -1);

                for (auto &s : slots)
                {
                    if (s == target_id) return;
                }

                json json_payload;
                json_payload["type"] = "request";
                json_payload["message"] = std::format("The client {} has requested to send you a file", c);
                json_payload["from"] = c;

                // send to target_id (assuming client id == fd)
                if (target_id >= 0) {
                    write_message(target_id, json_payload.dump());
                } else {
                    std::cerr << get_time() << "Invalid request target: " << target_id << std::endl;
                }

                sending_target = target_id;
            }
            else if (type == "un-pair")
            {
                int to_id = message.value("to", -1);
                if (to_id != sending_target) return;
                json json_payload;
                json_payload["type"] = "un-pair";
                json_payload["from"] = c;

                if (to_id >= 0) write_message(to_id, json_payload.dump());

                removeNumber(to_id);
                removeNumber(c);
            }
            else if (type == "accept")
            {
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "accept";
                json_payload["message"] = std::format("Client: {} accepted the file transfer request.", c);
                json_payload["from"] = c;

                std::cout << get_time() << "Client " << c << " accepted the file transfer request." << std::endl;

                if (to_id >= 0) write_message(to_id, json_payload.dump());

                if (!addNumber(to_id)) std::cout << "Error adding number to slots." << std::endl;
                if (!addNumber(c)) std::cout << "Error adding number to slots." << std::endl;

                sending_target = to_id;
            }
            else if (type == "reject")
            {
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "reject";
                json_payload["message"] = std::format("Client: {} rejected the file transfer request.", c);
                json_payload["from"] = c;

                if (to_id >= 0) write_message(to_id, json_payload.dump());

                sending_target = -1;
            }
            else if (type == "file_metadata")
            {
                // forward metadata to the "to" socket and then switch into raw forward mode
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "file_metadata";
                json_payload["file_name"] = message["file_name"];
                json_payload["file_size"] = message["file_size"];
                json_payload["from"] = c;

                if (to_id >= 0) {
                    write_message(to_id, json_payload.dump());

                    // set up raw-forwarding state:
                    total_file_size = message["file_size"].get<unsigned long long>();
                    sending_target = to_id;              // NOTE: we assume client id == fd
                    sending_file = true;
                    current_file_size = 0;
                    // create_buffer_sizes(total_file_size, buffer_sizes);

                    std::cout << get_time() << "Now forwarding raw file bytes from fd=" << c << " to fd=" << sending_target
                              << " expected_size=" << total_file_size << std::endl;
                } else {
                    std::cerr << get_time() << "Invalid file_metadata 'to' field: " << to_id << std::endl;
                }
            }
            else if (type == "directory_builder")
            {
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "directory_builder";
                json_payload["root_path"] = message["root_path"];
                json_payload["directories"] = message["directories"];
                json_payload["file_count"] = message["file_count"];
                json_payload["manifest"] = message["manifest"];
                json_payload["from"] = c;

                if (to_id >= 0) write_message(to_id, json_payload.dump());
                else std::cerr << get_time() << "Invalid directory_builder field: " << to_id << std::endl;
            }
            else if (type == "file_transfer_finished")
            {
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "file_transfer_finished";
                json_payload["message"] = message["message"];

                if (to_id >= 0) write_message(to_id, json_payload.dump());

                std::cout << get_time() << "file transfer finished" << std::endl;
            }
            else if (type == "message")
            {
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "message";
                json_payload["message"] = message["message"];
                json_payload["from"] = c;

                if (to_id >= 0) write_message(to_id, json_payload.dump());
            }
            else if (type == "encryption_request")
            {
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "encryption_request";
                json_payload["challenge"] = message["challenge"];
                if (to_id >= 0) write_message(to_id, json_payload.dump());
                else std::cerr << get_time() << "Invalid encryption request field: " << to_id << std::endl;
            }
            else if (type == "encryption_password_check")
            {
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "encryption_password_check";
                json_payload["response"] = message["response"];
                if (to_id >= 0) write_message(to_id, json_payload.dump());
                else std::cerr << get_time() << "Invalid encryption password_check field: " << to_id << std::endl;
            }
            else if (type == "encrypted_password_check_fail")
            {
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "encrypted_password_check_fail";
                if (to_id >= 0) write_message(to_id, json_payload.dump());
            }
            else if (type == "encrypted_password_check_pass")
            {
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "encrypted_password_check_pass";
                json_payload["nonce"] = message["nonce"];
                if (to_id >= 0) write_message(to_id, json_payload.dump());
            }
            else if (type == "encryption_off")
            {
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "encryption_off";
                if (to_id >= 0) write_message(to_id, json_payload.dump());
            }
            else if (type == "file_check")
            {
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "file_check";
                json_payload["file_path"] = message["file_path"];
                json_payload["hash"] = message["hash"];
                if (to_id >= 0) write_message(to_id, json_payload.dump());
            }
            else if (type == "file_check_reply")
            {
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "file_check_reply";
                json_payload["skip"] = message["skip"];
                if (to_id >= 0) write_message(to_id, json_payload.dump());
            }
            else if (type == "batch_file_check_reply")
            {
                int to_id = message.value("to", -1);
                json json_payload;
                json_payload["type"] = "batch_file_check_reply";
                json_payload["skip_list"] = message["skip_list"];
                if (to_id >= 0) write_message(to_id, json_payload.dump());
            }
            else
            {
                std::cerr << get_time() << "Unknown message type from fd=" << c << ": " << type << std::endl;
            }

            std::cout << get_time() << "Control message: " << message.dump() << std::endl;
        }
        catch (std::exception &e)
        {
            std::cout << get_time() << "Exception in client handler (fd=" << c << "): " << e.what() << std::endl;
            break;
        }
    }

    if (sending_target)
    {
        json json_payload;
        json_payload["type"] = "un-pair";
        json_payload["from"] = c;

        std::cout << get_time() << "Un pair request sent to " << sending_target << std::endl;

        if (sending_target >= 0) write_message(sending_target, json_payload.dump());
    }

    close(c);
    removeNumber(c);
    client_count = client_count - 1;
    std::cout << get_time() << "Client fd=" << c << " handler exiting\n";
}

void signal_handle(int)
{
    running = false;
    if (server_socket != -1)
    {
        std::cout << get_time() << "Closing socket" << std::endl;
        shutdown(server_socket, SHUT_RDWR);
        close(server_socket);
    }
}

int main(const int argc, char* argv[])
{
    auto host = IPADDRESS;
    int port = 3000;

    for (int i = 1; i < argc; i++)
    {
        if (strcmp(argv[i], "--host") == 0 && i + 1 < argc)
        {
            host = argv[++i];
        }
        else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            char* end = nullptr;
            errno = 0;
            const long value = std::strtol(argv[++i], &end, 10);

            if (errno != 0 || *end != '\0' || value < 1 || value > 65535)
                return 1;

            port = static_cast<int>(value);
        }
        else if (strcmp(argv[i], "--max-clients") == 0 && i + 1 < argc)
        {
            char* end = nullptr;
            errno = 0;
            const long value = std::strtol(argv[++i], &end, 10);

            if (errno != 0 || *end != '\0' || value <= 0)
                return 1;

            MAX_SIZE = static_cast<int>(value);
        }
        else if (strcmp(argv[i], "--chunk-size") == 0 && i + 1 < argc)
        {
            char* end = nullptr;
            errno = 0;
            const long value = std::strtol(argv[++i], &end, 10);

            if (errno != 0 || *end != '\0' || value < 0)
                return 1;

            CHUNK_SIZE = static_cast<int>(value);
        }
        else if (strcmp(argv[i], "--chunk-size-k") == 0 && i + 1 < argc)
        {
            char* end = nullptr;
            errno = 0;
            const long value = std::strtol(argv[++i], &end, 10);

            if (errno != 0 || *end != '\0' || value < 0)
                return 1;

            CHUNK_SIZE = static_cast<int>(value * 1024);
        }
    }

    signal(SIGINT, signal_handle);
    signal(SIGTERM, signal_handle);
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    server.sin_family = AF_INET;
    server.sin_port = htons(port);
    server.sin_addr.s_addr = inet_addr(host);

    initSlots();

    constexpr int opt = 1;
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    socklen_t server_address_length = sizeof(server);

    if (bind(server_socket, reinterpret_cast<sockaddr*>(&server), server_address_length) < 0) {
        perror("bind failed");
        return 1;
    }

    listen(server_socket, 6);

    while (running)
    {
        if (client_count > MAX_SIZE + 1) continue;
        sockaddr_in client_address {};
        socklen_t client_address_length = sizeof(client_address);
        const int client_int = accept(server_socket, reinterpret_cast<struct sockaddr*>(&client_address), &client_address_length);

        if (client_int < 0)
        {
            if (running) perror("accept failed");
            break;
        }

        std::thread(handle_client, client_int).detach();
        client_count = client_count + 1;
    }

    close(server_socket);
    return 0;
}