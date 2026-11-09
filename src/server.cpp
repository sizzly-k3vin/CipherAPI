#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

namespace UI {
    constexpr const char* RESET  = "\033[0m";
    constexpr const char* CYAN   = "\033[36m";
    constexpr const char* GREEN  = "\033[32m";
    constexpr const char* RED    = "\033[31m";
    constexpr const char* YELLOW = "\033[33m";
    constexpr const char* BOLD   = "\033[1m";
}

void banner() {
    std::cout << UI::CYAN << UI::BOLD;
    std::cout << R"(   _______       __              ___    ____  ____
  / ____(_)___  / /_  ___  _____/   |  / __ \/  _/
 / /   / / __ \/ __ \/ _ \/ ___/ /| | / /_/ // /
/ /___/ / /_/ / / / /  __/ /  / ___ |/ ____// /
\____/_/ .___/_/ /_/\___/_/  /_/  |_/_/   /___/
      /_/
)";
    std::cout << UI::RESET;
    std::cout << UI::YELLOW << "          CipherAPI SERVER v0.3\n";
    std::cout << "          =====================\n" << UI::RESET;
}

int main() {
    banner();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << UI::RED << "[ERROR] Socket creation failed\n" << UI::RESET;
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if (bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << UI::RED << "[ERROR] Port 8080 unavailable\n" << UI::RESET;
        perror("bind");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("listen");
        close(server_fd);
        return 1;
    }

    std::cout << UI::GREEN << "[+] Socket initialized\n";
    std::cout << "[+] Listening on 127.0.0.1:8080\n";
    std::cout << "[+] Status: ONLINE\n" << UI::RESET;
    std::cout << "-------------------------------------\n";

    while (true) {
        int client = accept(server_fd, nullptr, nullptr);
        if (client < 0) {
            perror("accept");
            continue;
        }

        std::cout << UI::CYAN << "[CONNECTION] Client connected\n" << UI::RESET;

        char buffer[4096]{};
        ssize_t n = recv(client, buffer, sizeof(buffer) - 1, 0);

        if (n > 0) {
            buffer[n] = '\0';
            std::cout << UI::YELLOW << "[RECEIVED] ";
            std::cout << UI::RESET << buffer << '\n';

            const char* reply = "CipherAPI: message received!";
            send(client, reply, strlen(reply), 0);

            std::cout << UI::GREEN << "[RESPONSE] Sent successfully\n";
            std::cout << UI::RESET;
        } else {
            std::cout << UI::RED << "[WARNING] Empty message\n";
            std::cout << UI::RESET;
        }

        close(client);
        std::cout << UI::CYAN << "[CONNECTION] Closed\n";
        std::cout << "-------------------------------------\n";
        std::cout << UI::RESET;
    }

    close(server_fd);
    return 0;
}
