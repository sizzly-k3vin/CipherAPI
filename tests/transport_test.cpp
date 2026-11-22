#include "transport.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <functional>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <cerrno>
#include <cstring>

using namespace CipherTransport;

static void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

static void test_round_trip() {
    int sockets[2];
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
            "socketpair failed: " + std::string(strerror(errno)));

    const std::string expected(128, 'A');
    std::string sender_error;

    std::thread sender([&] {
        try {
            send_frame(sockets[0], TEXT, expected);
        } catch (const std::exception& e) {
            sender_error = e.what();
        }
        close(sockets[0]);
    });

    uint8_t type = 0;
    std::string payload;
    bool received = false;
    std::string receiver_error;

    try {
        received = receive_frame(sockets[1], type, payload);
    } catch (const std::exception& e) {
        receiver_error = e.what();
    }

    sender.join();
    close(sockets[1]);

    require(sender_error.empty(), "sender: " + sender_error);
    require(receiver_error.empty(), "receiver: " + receiver_error);
    require(received, "receiver returned false");
    require(type == TEXT, "wrong frame type");
    require(payload == expected, "payload mismatch");
}

static void test_unknown_type() {
    int sockets[2];
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
            "socketpair failed");

    uint8_t header[] = {255, 0, 0, 0, 0};
    send_exact(sockets[0], header, sizeof(header));

    uint8_t type = 0;
    std::string payload;
    bool rejected = false;

    try {
        receive_frame(sockets[1], type, payload);
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    close(sockets[0]);
    close(sockets[1]);
    require(rejected, "unknown frame type was accepted");
}

static void test_oversized_frame() {
    int sockets[2];
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
            "socketpair failed");

    uint8_t type = IMAGE;
    uint32_t length = htonl(MAX_IMAGE_FRAME + 1);
    send_exact(sockets[0], &type, sizeof(type));
    send_exact(sockets[0], &length, sizeof(length));

    std::string payload;
    bool rejected = false;

    try {
        receive_frame(sockets[1], type, payload);
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    close(sockets[0]);
    close(sockets[1]);
    require(rejected, "oversized frame was accepted");
    require(payload.empty(), "payload allocated before size rejection");
}

static void test_invalid_text_length() {
    int sockets[2];
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
            "socketpair failed");

    uint8_t type = TEXT;
    uint32_t length = htonl(65);
    send_exact(sockets[0], &type, sizeof(type));
    send_exact(sockets[0], &length, sizeof(length));

    std::string payload;
    bool rejected = false;

    try {
        receive_frame(sockets[1], type, payload);
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    close(sockets[0]);
    close(sockets[1]);
    require(rejected, "invalid TEXT length was accepted");
    require(payload.empty(), "payload allocated before length rejection");
}

static void test_incomplete_header() {
    int sockets[2];
    require(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0,
            "socketpair failed");

    uint8_t partial[] = {TEXT, 0, 0};
    send_exact(sockets[0], partial, sizeof(partial));
    close(sockets[0]);

    uint8_t type = 0;
    std::string payload;
    bool rejected = false;

    try {
        receive_frame(sockets[1], type, payload);
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    close(sockets[1]);
    require(rejected, "incomplete header was not rejected");
}

static int run_test(const char* name, const std::function<void()>& test) {
    std::cout << "[ RUN  ] " << name << std::endl;
    try {
        test();
        std::cout << "[ PASS ] " << name << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "[ FAIL ] " << name << std::endl
                  << "         Reason: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "[ FAIL ] " << name
                  << "\n         Reason: unknown exception" << std::endl;
        return 1;
    }
}

int main() {
    int passed = 0;
    int failed = 0;

    auto run = [&](const char* name, const std::function<void()>& test) {
        if (run_test(name, test) == 0) ++passed;
        else ++failed;
    };

    run("valid frame round-trip", test_round_trip);
    run("unknown frame type", test_unknown_type);
    run("oversized frame rejection", test_oversized_frame);
    run("invalid TEXT length", test_invalid_text_length);
    run("incomplete frame header", test_incomplete_header);

    std::cout << "\n========== TRANSPORT TEST SUMMARY ==========\n"
              << "Passed: " << passed << std::endl
              << "Failed: " << failed << std::endl
              << "Total:  " << passed + failed << std::endl;

    return failed == 0 ? 0 : 1;
}
