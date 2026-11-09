#include <iostream>
#include <string>
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

int main() {
    std::cout << UI::CYAN << UI::BOLD;
    std::cout << R"(   _______       __              ___    ____  ____
  / ____(_)___  / /_  ___  _____/   |  / __ \/  _/
 / /   / / __ \/ __ \/ _ \/ ___/ /| | / /_/ // /
/ /___/ / /_/ / / / /  __/ /  / ___ |/ ____// /
\____/_/ .___/_/ /_/\___/_/  /_/  |_/_/   /___/
      /_/
)";
    std::cout << UI::RESET;

    std::cout << UI::YELLOW << "          CipherAPI CLIENT v0.3\n";
    std::cout << "          =====================\n" << UI::RESET;

    std::cout << UI::CYAN << "[*] Initializing connection...\n" << UI::RESET;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        std::cerr << UI::RED << "[ERROR] Socket creation failed\n" << UI::RESET;
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if (connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << UI::RED << "[ERROR] Connection failed\n" << UI::RESET;
        perror("connect");
        close(fd);
        return 1;
    }

    std::cout << UI::GREEN << "[+] Connected to CipherAPI server\n";
    std::cout << "[+] Connection status: ONLINE\n" << UI::RESET;

    std::string msg;
    std::cout << UI::YELLOW << "\nMessage > " << UI::RESET;
    std::getline(std::cin, msg);

    if (send(fd, msg.data(), msg.size(), 0) < 0) {
        std::cerr << UI::RED << "[ERROR] Message transmission failed\n" << UI::RESET;
        close(fd);
        return 1;
    }

    std::cout << UI::CYAN << "[TX] Message transmitted\n" << UI::RESET;

    char buffer[4096]{};
    ssize_t n = recv(fd, buffer, sizeof(buffer) - 1, 0);

    if (n > 0) {
        buffer[n] = '\0';
        std::cout << UI::GREEN << "[RX] Server response received\n";
        std::cout << UI::RESET;
        std::cout << UI::BOLD << "Server >> " << UI::RESET << buffer << '\n';
    } else {
        std::cout << UI::RED << "[ERROR] No response received\n" << UI::RESET;
    }

    close(fd);
    std::cout << UI::CYAN << "\n[*] Session terminated\n" << UI::RESET;

    return 0;
}
