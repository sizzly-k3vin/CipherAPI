#include <iostream>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

int main() {
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if(server_fd < 0) {
        perror("socket");
        return 1;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    if(bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    if(listen(server_fd, 5) < 0) {
        perror("listen");
        return 1;
    }

    std::cout << "CipherAPI listening on 127.0.0.1:8080" << std::endl;

    while(true) {
        int client = accept(server_fd, nullptr, nullptr);
        if(client < 0) {
            perror("accept");
            continue;
        }

        char buffer[4096]{};
        ssize_t n = recv(client, buffer, sizeof(buffer)-1, 0);

        if(n > 0) {
            buffer[n] = 0;

            std::cout << "[CLIENT] " << buffer << std::endl;

            const char* reply = "CipherAPI: message received!";
            send(client, reply, strlen(reply), 0);
        }

        close(client);
    }

    close(server_fd);
}
