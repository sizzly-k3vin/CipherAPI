#include "debug.hpp"
#include "image.hpp"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr std::size_t CHECKSUM_SIZE = 64;
constexpr std::size_t MAX_IMAGE_SIZE = 32 * 1024 * 1024;

std::string sha256(const std::string& data) {
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digest_length = 0;

    if (EVP_Digest(
            data.data(), data.size(),
            digest.data(), &digest_length,
            EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("SHA-256 calculation failed");
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');

    for (unsigned int i = 0; i < digest_length; ++i)
        output << std::setw(2) << static_cast<unsigned>(digest[i]);

    return output.str();
}

bool valid_image_extension(const std::string& name) {
    std::string ext = fs::path(name).extension().string();

    std::transform(ext.begin(), ext.end(), ext.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    return ext == ".png" || ext == ".jpg" ||
           ext == ".jpeg" || ext == ".webp" ||
           ext == ".gif" || ext == ".bmp";
}

uint16_t read_u16(const std::string& data, std::size_t offset) {
    return static_cast<uint16_t>(
        (static_cast<unsigned char>(data[offset]) << 8) |
         static_cast<unsigned char>(data[offset + 1])
    );
}

}

namespace ImageLibrary {

std::string pack(const std::string& path) {
    fs::path source(path);

    if (!fs::exists(source) || !fs::is_regular_file(source))
        throw std::runtime_error("Image file not found");

    std::string filename = source.filename().string();

    if (filename.empty() || filename.size() > 255)
        throw std::runtime_error("Invalid image filename");

    if (!valid_image_extension(filename))
        throw std::runtime_error("Unsupported image extension");

    const auto file_size = fs::file_size(source);

    if (file_size > MAX_IMAGE_SIZE - 512)
        throw std::runtime_error("Image exceeds 32 MiB limit");

    std::ifstream file(source, std::ios::binary);

    if (!file)
        throw std::runtime_error("Could not open image");

    std::string bytes{
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>()
    };

    if (!file.eof() && file.fail())
        throw std::runtime_error("Image read failed");

    if (bytes.empty())
        throw std::runtime_error("Image is empty");

    const std::string checksum = sha256(bytes);

    // Packet: 2-byte filename length | filename | 64-byte checksum | image
    std::string packet;

    uint16_t name_length = static_cast<uint16_t>(filename.size());

    packet.push_back(static_cast<char>((name_length >> 8) & 0xFF));
    packet.push_back(static_cast<char>(name_length & 0xFF));

    packet += filename;
    packet += checksum;
    packet += bytes;

    return packet;
}

std::string unpack(const std::string& packet,
                   const std::string& output_directory) {
    if (packet.size() < 2 + CHECKSUM_SIZE)
        throw std::runtime_error("Image packet is too small");

    const uint16_t name_length = read_u16(packet, 0);

    if (name_length == 0 || name_length > 255)
        throw std::runtime_error("Invalid filename metadata");

    const std::size_t checksum_offset = 2 + name_length;
    const std::size_t image_offset = checksum_offset + CHECKSUM_SIZE;

    if (packet.size() <= image_offset)
        throw std::runtime_error("Image data is missing");

    std::string filename = packet.substr(2, name_length);

    // Strip any directory components supplied by the sender.
    std::replace(filename.begin(), filename.end(), '\\', '/');
    filename = fs::path(filename).filename().string();

    if (filename.empty() || !valid_image_extension(filename))
        throw std::runtime_error("Invalid received image filename");

    const std::string expected =
        packet.substr(checksum_offset, CHECKSUM_SIZE);

    const std::string bytes = packet.substr(image_offset);
    const std::string actual = sha256(bytes);

    if (actual != expected)
        throw std::runtime_error("SHA-256 verification FAILED");

    fs::create_directories(output_directory);

    fs::path destination = fs::path(output_directory) /
                           ("received_" + filename);

    // Avoid overwriting an earlier received image.
    for (unsigned int i = 1; fs::exists(destination); ++i) {
        destination = fs::path(output_directory) /
            ("received_" + std::to_string(i) + "_" + filename);
    }

    std::ofstream output(destination, std::ios::binary);

    if (!output)
        throw std::runtime_error("Could not create received image");

    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));

    if (!output)
        throw std::runtime_error("Image write failed");

    return destination.string();
}

}
