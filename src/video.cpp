#include "video.hpp"
#include "transport.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace CipherVideo {
namespace {

constexpr std::uint8_t VERSION = 1;
constexpr std::size_t HASH_SIZE = 32;
constexpr std::size_t CHUNK_HEADER_SIZE = 14;

void put_u16(std::string& out, std::uint16_t v) {
    out.push_back(static_cast<char>(v >> 8));
    out.push_back(static_cast<char>(v));
}

void put_u32(std::string& out, std::uint32_t v) {
    for (int i = 3; i >= 0; --i)
        out.push_back(static_cast<char>(v >> (i * 8)));
}

void put_u64(std::string& out, std::uint64_t v) {
    for (int i = 7; i >= 0; --i)
        out.push_back(static_cast<char>(v >> (i * 8)));
}

std::uint16_t get_u16(const std::string& s, std::size_t p) {
    return (static_cast<std::uint16_t>(
                static_cast<unsigned char>(s[p])) << 8) |
           static_cast<unsigned char>(s[p + 1]);
}

std::uint32_t get_u32(const std::string& s, std::size_t p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v = (v << 8) | static_cast<unsigned char>(s[p + i]);
    return v;
}

std::uint64_t get_u64(const std::string& s, std::size_t p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v = (v << 8) | static_cast<unsigned char>(s[p + i]);
    return v;
}

// A deterministic generator: both ends produce the same permutation.
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}

    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9e3779b97f4a7c15ULL);
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

private:
    std::uint64_t state_;
};

std::vector<std::uint32_t> make_permutation(
    std::size_t count, std::uint64_t seed) {

    std::vector<std::uint32_t> permutation(count);
    std::iota(permutation.begin(), permutation.end(), 0);

    SplitMix64 rng(seed);

    for (std::size_t i = count; i > 1; --i) {
        const std::size_t j =
            static_cast<std::size_t>(rng.next() % i);
        std::swap(permutation[i - 1], permutation[j]);
    }

    return permutation;
}

std::string bytes_to_bits(const std::string& bytes) {
    std::string bits;
    bits.reserve(bytes.size() * 8);

    for (unsigned char byte : bytes) {
        for (int bit = 7; bit >= 0; --bit)
            bits.push_back((byte & (1u << bit)) ? '1' : '0');
    }

    return bits;
}

std::string bits_to_bytes(const std::string& bits) {
    if (bits.size() % 8 != 0)
        throw std::runtime_error("Invalid reconstructed bit count");

    std::string bytes(bits.size() / 8, '\0');

    for (std::size_t i = 0; i < bits.size(); ++i) {
        if (bits[i] != '0' && bits[i] != '1')
            throw std::runtime_error("Invalid bit in shuffled data");

        if (bits[i] == '1')
            bytes[i / 8] |= static_cast<char>(1u << (7 - (i % 8)));
    }

    return bytes;
}

std::string shuffle_bits(
    const std::string& original, std::uint64_t seed) {

    const auto permutation =
        make_permutation(original.size(), seed);

    std::string shuffled(original.size(), '0');

    for (std::size_t i = 0; i < original.size(); ++i)
        shuffled[i] = original[permutation[i]];

    return shuffled;
}

std::string unshuffle_bits(
    const std::string& shuffled, std::uint64_t seed) {

    const auto permutation =
        make_permutation(shuffled.size(), seed);

    std::string original(shuffled.size(), '0');

    for (std::size_t i = 0; i < shuffled.size(); ++i)
        original[permutation[i]] = shuffled[i];

    return original;
}

void digest_update(EVP_MD_CTX* ctx, const std::string& bytes) {
    if (EVP_DigestUpdate(ctx, bytes.data(), bytes.size()) != 1)
        throw std::runtime_error("SHA-256 update failed");
}

std::string digest_final(EVP_MD_CTX* ctx) {
    std::string digest(HASH_SIZE, '\0');
    unsigned int length = 0;

    if (EVP_DigestFinal_ex(
            ctx,
            reinterpret_cast<unsigned char*>(digest.data()),
            &length) != 1 || length != HASH_SIZE) {
        throw std::runtime_error("SHA-256 finalization failed");
    }

    return digest;
}

std::string safe_output_name(const std::string& name) {
    if (name.empty() || name.size() > 255 ||
        name == "." || name == ".." ||
        fs::path(name).filename().string() != name ||
        name.find('/') != std::string::npos ||
        name.find('\\') != std::string::npos) {
        throw std::runtime_error("Unsafe video filename");
    }

    return name;
}

} // namespace

class Receiver::State {
public:
    std::ofstream file;
    EVP_MD_CTX* digest = nullptr;

    ~State() {
        if (digest)
            EVP_MD_CTX_free(digest);
    }
};

TransferStats send_file(
    int fd,
    const LibraryManager& libraries,
    const std::string& path,
    bool use_sha256) {

    const fs::path source(path);
    const std::string filename = source.filename().string();

    if (filename.empty() || filename.size() > 255)
        throw std::runtime_error("Invalid video filename");

    std::error_code ec;
    const std::uint64_t file_size = fs::file_size(source, ec);

    if (ec)
        throw std::runtime_error("Cannot read video file size: " + ec.message());

    if (file_size == 0)
        throw std::runtime_error("Video file is empty");

    std::ifstream input(source, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot open video file");

    EVP_MD_CTX* digest = nullptr;

    if (use_sha256) {
        digest = EVP_MD_CTX_new();
        if (!digest)
            throw std::runtime_error("Cannot allocate SHA-256 context");

        if (EVP_DigestInit_ex(digest, EVP_sha256(), nullptr) != 1) {
            EVP_MD_CTX_free(digest);
            throw std::runtime_error("Cannot initialize SHA-256");
        }
    }

    TransferStats stats;
    stats.original_bytes = file_size;

    try {
        // VIDEO_START: version, flags, filename length, file size, filename.
        std::string start;
        start.push_back(static_cast<char>(VERSION));
        start.push_back(static_cast<char>(use_sha256 ? 1 : 0));
        put_u16(start, static_cast<std::uint16_t>(filename.size()));
        put_u64(start, file_size);
        start += filename;

        CipherTransport::send_frame(
            fd, CipherTransport::VIDEO_START, start);
        stats.wire_bytes += start.size();

        std::uint64_t remaining = file_size;
        std::uint32_t sequence = 0;

        while (remaining > 0) {
            const std::size_t amount = static_cast<std::size_t>(
                std::min<std::uint64_t>(CHUNK_SIZE, remaining));

            std::string bytes(amount, '\0');
            input.read(bytes.data(), static_cast<std::streamsize>(amount));

            if (input.gcount() != static_cast<std::streamsize>(amount))
                throw std::runtime_error("Video file changed or read failed");

            if (use_sha256)
                digest_update(digest, bytes);

            std::uint64_t seed = 0;
            if (RAND_bytes(
                    reinterpret_cast<unsigned char*>(&seed),
                    sizeof(seed)) != 1) {
                throw std::runtime_error("Random seed generation failed");
            }

            const std::string bits = bytes_to_bits(bytes);
            const std::string shuffled = shuffle_bits(bits, seed);

            // The existing ten libraries encode the shuffled 0/1 characters.
            const std::string encoded = libraries.encode(shuffled);

            // VIDEO_DATA: sequence, seed, original byte count, encoded bits.
            std::string packet;
            packet.reserve(CHUNK_HEADER_SIZE + encoded.size());

            put_u32(packet, sequence);
            put_u64(packet, seed);
            put_u16(packet, static_cast<std::uint16_t>(amount));
            packet += encoded;

            CipherTransport::send_frame(
                fd, CipherTransport::VIDEO_DATA, packet);

            stats.wire_bytes += packet.size();
            ++stats.chunks;
            ++sequence;
            remaining -= amount;
        }

        if (input.peek() != std::char_traits<char>::eof())
            throw std::runtime_error("Video file grew during transfer");

        // VIDEO_END: total original bytes, chunk count, optional digest.
        std::string end;
        put_u64(end, file_size);
        put_u32(end, stats.chunks);

        if (use_sha256)
            end += digest_final(digest);

        CipherTransport::send_frame(
            fd, CipherTransport::VIDEO_END, end);
        stats.wire_bytes += end.size();

        if (digest)
            EVP_MD_CTX_free(digest);

        return stats;
    } catch (...) {
        if (digest)
            EVP_MD_CTX_free(digest);
        throw;
    }
}

Receiver::Receiver(const LibraryManager& libraries)
    : libraries_(libraries), state_(new State()) {}

Receiver::~Receiver() {
    abort_transfer();
    delete state_;
}

void Receiver::abort_transfer() noexcept {
    if (state_ && state_->file.is_open())
        state_->file.close();

    if (!temporary_path_.empty()) {
        std::error_code ec;
        fs::remove(temporary_path_, ec);
    }

    active_ = false;
    expected_size_ = 0;
    received_size_ = 0;
    expected_sequence_ = 0;
    use_sha256_ = false;
    final_path_.clear();
    temporary_path_.clear();

    if (state_ && state_->digest) {
        EVP_MD_CTX_free(state_->digest);
        state_->digest = nullptr;
    }
}

std::string Receiver::handle(
    std::uint8_t type, const std::string& payload) {

    if (type == CipherTransport::VIDEO_START) {
        if (active_)
            throw std::runtime_error("A video transfer is already active");

        if (payload.size() < 12)
            throw std::runtime_error("Invalid VIDEO_START header");

        if (static_cast<std::uint8_t>(payload[0]) != VERSION)
            throw std::runtime_error("Unsupported CVS version");

        const std::uint8_t flags =
            static_cast<std::uint8_t>(payload[1]);

        if (flags > 1)
            throw std::runtime_error("Invalid video checksum mode");

        const std::uint16_t name_length = get_u16(payload, 2);
        expected_size_ = get_u64(payload, 4);

        if (name_length == 0 || name_length > 255 ||
            payload.size() != 12 + name_length ||
            expected_size_ == 0) {
            throw std::runtime_error("Invalid video metadata");
        }

        const std::string filename =
            safe_output_name(payload.substr(12, name_length));

        use_sha256_ = flags == 1;

        if (use_sha256_) {
            state_->digest = EVP_MD_CTX_new();
            if (!state_->digest ||
                EVP_DigestInit_ex(
                    state_->digest, EVP_sha256(), nullptr) != 1) {
                throw std::runtime_error("Cannot initialize receive SHA-256");
            }
        }

        std::string candidate = "received_" + filename;
        unsigned int suffix = 1;

        while (fs::exists(candidate) || fs::exists(candidate + ".part")) {
            candidate = "received_" + filename + "." +
                        std::to_string(suffix++);
        }

        final_path_ = candidate;
        temporary_path_ = final_path_ + ".part";

        state_->file.open(
            temporary_path_, std::ios::binary | std::ios::out);

        if (!state_->file)
            throw std::runtime_error("Cannot create temporary video file");

        received_size_ = 0;
        expected_sequence_ = 0;
        active_ = true;
        return {};
    }

    if (!active_)
        throw std::runtime_error("Video data received without VIDEO_START");

    if (type == CipherTransport::VIDEO_DATA) {
        if (payload.size() < CHUNK_HEADER_SIZE)
            throw std::runtime_error("Invalid VIDEO_DATA header");

        const std::uint32_t sequence = get_u32(payload, 0);
        const std::uint64_t seed = get_u64(payload, 4);
        const std::uint16_t raw_size = get_u16(payload, 12);

        if (sequence != expected_sequence_)
            throw std::runtime_error("Video chunk sequence mismatch");

        if (raw_size == 0 || raw_size > CHUNK_SIZE ||
            raw_size > expected_size_ - received_size_) {
            throw std::runtime_error("Invalid video chunk size");
        }

        const std::string encoded =
            payload.substr(CHUNK_HEADER_SIZE);

        if (encoded.size() !=
            static_cast<std::size_t>(raw_size) * 8 * 64) {
            throw std::runtime_error("Invalid encoded video chunk length");
        }

        const std::string shuffled = libraries_.decode(encoded);

        if (shuffled.size() != static_cast<std::size_t>(raw_size) * 8)
            throw std::runtime_error("Decoded bit count mismatch");

        const std::string bits = unshuffle_bits(shuffled, seed);
        const std::string bytes = bits_to_bytes(bits);

        if (bytes.size() != raw_size)
            throw std::runtime_error("Reconstructed chunk size mismatch");

        state_->file.write(
            bytes.data(), static_cast<std::streamsize>(bytes.size()));

        if (!state_->file)
            throw std::runtime_error("Video disk write failed");

        if (use_sha256_)
            digest_update(state_->digest, bytes);

        received_size_ += bytes.size();
        ++expected_sequence_;
        return {};
    }

    if (type == CipherTransport::VIDEO_END) {
        const std::size_t expected_end_size =
            use_sha256_ ? 12 + HASH_SIZE : 12;

        if (payload.size() != expected_end_size)
            throw std::runtime_error("Invalid VIDEO_END packet");

        const std::uint64_t final_size = get_u64(payload, 0);
        const std::uint32_t final_chunks = get_u32(payload, 8);

        const std::uint64_t expected_chunks =
            (expected_size_ + CHUNK_SIZE - 1) / CHUNK_SIZE;

        if (final_size != expected_size_ ||
            received_size_ != expected_size_ ||
            final_chunks != expected_sequence_ ||
            final_chunks != expected_chunks) {
            throw std::runtime_error("Video transfer size or chunk mismatch");
        }

        if (use_sha256_) {
            const std::string actual = digest_final(state_->digest);
            const std::string claimed = payload.substr(12, HASH_SIZE);

            if (CRYPTO_memcmp(
                    actual.data(), claimed.data(), HASH_SIZE) != 0) {
                throw std::runtime_error("Video SHA-256 verification failed");
            }
        }

        state_->file.flush();
        if (!state_->file)
            throw std::runtime_error("Video file flush failed");

        state_->file.close();

        std::error_code ec;
        fs::rename(temporary_path_, final_path_, ec);

        if (ec)
            throw std::runtime_error("Cannot finalize video file: " +
                                     ec.message());

        const std::string saved = final_path_;

        active_ = false;
        expected_size_ = 0;
        received_size_ = 0;
        expected_sequence_ = 0;
        use_sha256_ = false;
        final_path_.clear();
        temporary_path_.clear();

        if (state_->digest) {
            EVP_MD_CTX_free(state_->digest);
            state_->digest = nullptr;
        }

        return saved;
    }

    throw std::runtime_error("Unexpected frame during video transfer");
}

} // namespace CipherVideo
