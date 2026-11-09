#include <iostream>
#include <string>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main() {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if(fd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    if(connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }

    std::string msg;
    std::cout << "Message: ";
    std::getline(std::cin, msg);

    if(send(fd, msg.data(), msg.size(), 0) < 0) {
        perror("send");
        close(fd);
        return 1;
    }

    char buffer[4096]{};
    ssize_t n = recv(fd, buffer, sizeof(buffer)-1, 0);

    if(n > 0) {
        buffer[n] = 0;
        std::cout << "Server: " << buffer << std::endl;
    }

    close(fd);
}
