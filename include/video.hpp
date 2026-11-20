#pragma once

#include "library.hpp"

#include <cstdint>
#include <string>

namespace CipherVideo {

constexpr std::uint32_t CHUNK_SIZE = 4096;
constexpr std::uint64_t MAX_VIDEO_SIZE = 2ULL * 1024 * 1024 * 1024;

struct TransferStats {
    std::uint64_t original_bytes = 0;
    std::uint64_t wire_bytes = 0;
    std::uint32_t chunks = 0;
};

TransferStats send_file(
    int fd,
    const LibraryManager& libraries,
    const std::string& path,
    bool use_sha256,
    bool fast_mode
);

class Receiver {
public:
    explicit Receiver(const LibraryManager& libraries);
    ~Receiver();

    Receiver(const Receiver&) = delete;
    Receiver& operator=(const Receiver&) = delete;

    // Returns the saved filename when VIDEO_END completes a transfer.
    std::string handle(std::uint8_t type, const std::string& payload);
    void abort() noexcept;

private:
    const LibraryManager& libraries_;
    bool active_ = false;
    bool use_sha256_ = false;
    bool fast_mode_ = false;
    std::uint64_t expected_size_ = 0;
    std::uint64_t received_size_ = 0;
    std::uint32_t expected_sequence_ = 0;
    std::string final_path_;
    std::string temporary_path_;
    class State;
    State* state_ = nullptr;

    void abort_transfer() noexcept;
};

}
