#pragma once

#include <string>

namespace ImageLibrary {

std::string pack(const std::string& path);

std::string unpack(
    const std::string& packet,
    const std::string& output_directory = "received_images"
);

}
