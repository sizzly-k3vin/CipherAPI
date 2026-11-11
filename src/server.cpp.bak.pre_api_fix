#include "debug.hpp"
#include "library.hpp"
#include "image.hpp"
#include "logger.hpp"
#include "transport.hpp"
#include "neon.hpp"
#include "video.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

struct Client {
    int id;
    int fd;
    std::atomic<bool> connected{true};
    std::atomic<bool> fast{false};
    Neon::Stats stats;
    std::mutex send_mutex;
    std::thread receiver;
};

static std::atomic<bool> running{true};
static std::mutex clients_mutex;
static std::vector<std::shared_ptr<Client>> clients;

static void disconnect_client(const std::shared_ptr<Client>& c) {
    if (c->connected.exchange(false))
        shutdown(c->fd, SHUT_RDWR);
}

static void send_text(
    const std::shared_ptr<Client>& c,
    const LibraryManager& libraries,
    const std::string& message
) {
    std::lock_guard<std::mutex> lock(c->send_mutex);

    if (!c->connected)
        throw std::runtime_error("Client disconnected");

    std::string payload = c->fast
        ? message
        : libraries.encode(message);

    auto type = c->fast
        ? CipherTransport::FAST_TEXT
        : CipherTransport::TEXT;

    if (!CipherTransport::send_frame(c->fd, type, payload))
        throw std::runtime_error("Send failed");

    ++c->stats.sent_messages;
    c->stats.sent_chars += message.size();
    c->stats.sent_bytes += payload.size();

    Neon::print_message("SERVER -> CLIENT " +
                        std::to_string(c->id), message);
}

static void receive_client(
    std::shared_ptr<Client> c,
    const LibraryManager& libraries
) {
    CipherVideo::Receiver video_receiver(libraries);

    try {
        while (running && c->connected) {
            uint8_t type = 0;
            std::string payload;

            if (!CipherTransport::receive_frame(c->fd, type, payload))
                break;

            ++c->stats.received_bytes;
            c->stats.received_bytes += payload.size();

            if (type == CipherTransport::TEXT) {
                std::string message = libraries.decode(payload);
                ++c->stats.received_messages;
                c->stats.received_chars += message.size();
                Neon::decode("CLIENT " + std::to_string(c->id),
                             message);
                Neon::print_message("CLIENT " +
                                    std::to_string(c->id), message);

                if (message == "/quit")
                    break;
            }
            else if (type == CipherTransport::FAST_TEXT) {
                ++c->stats.received_messages;
                c->stats.received_chars += payload.size();
                Neon::print_message("CLIENT " +
                                    std::to_string(c->id), payload);

                if (payload == "/quit")
                    break;
            }
            else if (type == CipherTransport::IMAGE) {
                std::filesystem::path dir =
                    "received_images/client_" + std::to_string(c->id);
                std::filesystem::create_directories(dir);
                std::string path =
                    ImageLibrary::unpack(payload, dir.string());
                Neon::info("Image received: " + path);
            }
            else if (type == CipherTransport::RAW_IMAGE) {
                std::filesystem::path dir =
                    "received_images/client_" + std::to_string(c->id);
                std::filesystem::create_directories(dir);
                std::string path =
                    ImageLibrary::unpack_raw(payload, dir.string());
                Neon::info("Raw image received: " + path);
            }
            else if (type >= CipherTransport::VIDEO_START &&
                     type <= CipherTransport::VIDEO_END) {
                std::string result = video_receiver.handle(type, payload);
                if (!result.empty())
                    Neon::info("Video received: " + result);
            }
            else {
                Neon::error("Unknown frame from client " +
                            std::to_string(c->id));
            }
        }
    }
    catch (const std::exception& e) {
        Neon::error("Client " + std::to_string(c->id) +
                    ": " + e.what());
    }

    disconnect_client(c);
    Neon::info("Client " + std::to_string(c->id) + " disconnected");
}

static std::shared_ptr<Client> find_client(int id) {
    std::lock_guard<std::mutex> lock(clients_mutex);

    for (auto& c : clients)
        if (c->id == id && c->connected)
            return c;

    return nullptr;
}

static void cleanup_clients() {
    std::vector<std::shared_ptr<Client>> dead;

    {
        std::lock_guard<std::mutex> lock(clients_mutex);

        for (auto it = clients.begin(); it != clients.end();) {
            if (!(*it)->connected) {
                dead.push_back(*it);
                it = clients.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (auto& c : dead) {
        if (c->receiver.joinable())
            c->receiver.join();

        close(c->fd);
    }
}

static void transmit_image(
    const std::shared_ptr<Client>& c,
    const std::string& path,
    bool raw
) {
    std::lock_guard<std::mutex> lock(c->send_mutex);

    if (!c->connected)
        throw std::runtime_error("Client disconnected");

    std::string packet = raw
        ? ImageLibrary::pack_raw(path)
        : ImageLibrary::pack(path);

    auto type = raw
        ? CipherTransport::RAW_IMAGE
        : CipherTransport::IMAGE;

    if (!CipherTransport::send_frame(c->fd, type, packet))
        throw std::runtime_error("Image send failed");

    ++c->stats.sent_messages;
    c->stats.sent_bytes += packet.size();
}

static void transmit_video(
    const std::shared_ptr<Client>& c,
    const LibraryManager& libraries,
    const std::string& path,
    bool sha
) {
    std::lock_guard<std::mutex> lock(c->send_mutex);

    if (!c->connected)
        throw std::runtime_error("Client disconnected");

    CipherVideo::send_file(c->fd, libraries, path, sha);
}

static void show_clients() {
    std::lock_guard<std::mutex> lock(clients_mutex);

    std::cout << "\n--- ACTIVE CLIENTS ---\n";

    for (const auto& c : clients) {
        if (c->connected)
            std::cout << "ID " << c->id
                      << " | " << (c->fast ? "FAST" : "ENCODED")
                      << '\n';
    }

    std::cout << "----------------------\n";
}

int main() {
    try {
        LibraryManager libraries("libraries");

        int server = socket(AF_INET, SOCK_STREAM, 0);
        if (server < 0)
            throw std::runtime_error("Socket creation failed");

        int opt = 1;
        setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(8080);

        if (bind(server, reinterpret_cast<sockaddr*>(&address),
                 sizeof(address)) < 0)
            throw std::runtime_error("Bind failed");

        if (listen(server, 20) < 0)
            throw std::runtime_error("Listen failed");

        Neon::banner();
        Neon::info("CipherAPI multi-client server started on port 8080");

        std::thread accept_thread([&]() {
            int next_id = 1;

            while (running) {
                pollfd pfd{server, POLLIN, 0};
                int ready = poll(&pfd, 1, 500);

                if (!running)
                    break;

                if (ready <= 0)
                    continue;

                sockaddr_in client_addr{};
                socklen_t len = sizeof(client_addr);

                int fd = accept(server,
                    reinterpret_cast<sockaddr*>(&client_addr), &len);

                if (fd < 0)
                    continue;

                auto c = std::make_shared<Client>();
                c->id = next_id++;
                c->fd = fd;

                c->receiver = std::thread(
                    receive_client, c, std::cref(libraries));

                {
                    std::lock_guard<std::mutex> lock(clients_mutex);
                    clients.push_back(c);
                }

                Neon::info("Client " + std::to_string(c->id) +
                           " connected from " +
                           std::string(inet_ntoa(client_addr.sin_addr)));
            }
        });

        int selected_id = -1;
        std::string line;

        Neon::info("Type /help for commands");

        while (running) {
            cleanup_clients();

            std::cout << "CipherAPI";
            if (selected_id != -1)
                std::cout << "[" << selected_id << "]";
            std::cout << "> " << std::flush;

            if (!std::getline(std::cin, line))
                break;

            if (line.empty())
                continue;

            std::istringstream input(line);
            std::string command;
            input >> command;

            try {
                if (command == "/help") {
                    std::cout
                        << "\n/help                 Show commands\n"
                        << "/clients              List clients\n"
                        << "/use ID               Select client\n"
                        << "/to ID MESSAGE        Direct message\n"
                        << "/broadcast MESSAGE    Send to everyone\n"
                        << "/stats                Selected client stats\n"
                        << "/fast                 Toggle fast mode\n"
                        << "/sendimg FILE         Send image\n"
                        << "/sendimg -f FILE      Send raw image\n"
                        << "/sendvid FILE         Send video\n"
                        << "/sendvid -f FILE      Send video with SHA-256\n"
                        << "/quit                 Disconnect selected client\n"
                        << "/shutdown             Stop server\n";
                }
                else if (command == "/clients") {
                    show_clients();
                }
                else if (command == "/use") {
                    int id;
                    if (!(input >> id)) {
                        Neon::error("Usage: /use ID");
                        continue;
                    }

                    if (find_client(id)) {
                        selected_id = id;
                        Neon::info("Selected client " + std::to_string(id));
                    } else {
                        Neon::error("Client not found");
                    }
                }
                else if (command == "/to") {
                    int id;
                    if (!(input >> id)) {
                        Neon::error("Usage: /to ID MESSAGE");
                        continue;
                    }

                    std::string message;
                    std::getline(input >> std::ws, message);

                    auto c = find_client(id);
                    if (!c || message.empty()) {
                        Neon::error("Client not found or message empty");
                        continue;
                    }

                    send_text(c, libraries, message);
                }
                else if (command == "/broadcast") {
                    std::string message;
                    std::getline(input >> std::ws, message);

                    if (message.empty())
                        continue;

                    std::vector<std::shared_ptr<Client>> snapshot;
                    {
                        std::lock_guard<std::mutex> lock(clients_mutex);
                        snapshot = clients;
                    }

                    for (auto& c : snapshot) {
                        if (!c->connected)
                            continue;

                        try {
                            send_text(c, libraries, message);
                        } catch (...) {
                            disconnect_client(c);
                        }
                    }
                }
                else if (command == "/stats") {
                    auto c = find_client(selected_id);
                    if (c)
                        Neon::show_stats(c->stats);
                    else
                        Neon::error("No active client selected");
                }
                else if (command == "/fast") {
                    auto c = find_client(selected_id);
                    if (!c) {
                        Neon::error("Select a client first");
                        continue;
                    }

                    c->fast = !c->fast;
                    Neon::info(std::string("Fast mode: ") +
                               (c->fast ? "ON" : "OFF"));
                }
                else if (command == "/sendimg" ||
                         command == "/sendvid") {
                    auto c = find_client(selected_id);
                    if (!c) {
                        Neon::error("Select a client first");
                        continue;
                    }

                    std::string option, path;
                    input >> option;

                    bool raw = option == "-f";
                    if (raw)
                        input >> path;
                    else
                        path = option;

                    if (path.empty()) {
                        Neon::error("File path required");
                        continue;
                    }

                    if (command == "/sendimg")
                        transmit_image(c, path, raw);
                    else
                        transmit_video(c, libraries, path, raw);
                }
                else if (command == "/quit") {
                    auto c = find_client(selected_id);
                    if (!c) {
                        Neon::error("No active client selected");
                        continue;
                    }

                    try {
                        send_text(c, libraries, "/quit");
                    } catch (...) {}

                    disconnect_client(c);
                    selected_id = -1;
                }
                else if (command == "/shutdown") {
                    running = false;
                    break;
                }
                else {
                    auto c = find_client(selected_id);
                    if (!c) {
                        Neon::error("Select a client using /use ID");
                        continue;
                    }

                    send_text(c, libraries, line);
                }
            }
            catch (const std::exception& e) {
                Neon::error(e.what());

                auto c = find_client(selected_id);
                if (c && !c->connected)
                    selected_id = -1;
            }
        }

        running = false;
        shutdown(server, SHUT_RDWR);

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (auto& c : clients)
                disconnect_client(c);
        }

        if (accept_thread.joinable())
            accept_thread.join();

        cleanup_clients();

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            for (auto& c : clients) {
                disconnect_client(c);
                if (c->receiver.joinable())
                    c->receiver.join();
                close(c->fd);
            }
            clients.clear();
        }

        close(server);
        Neon::info("CipherAPI server stopped");
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << "FATAL: " << e.what() << '\n';
        return 1;
    }
}
