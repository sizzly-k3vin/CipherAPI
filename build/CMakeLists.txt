cmake_minimum_required(VERSION 3.20)
project(CipherLink LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(cipherlink_core src/profile.cpp)
target_include_directories(cipherlink_core PUBLIC include)

add_executable(cipher_server src/server.cpp)
target_link_libraries(cipher_server PRIVATE cipherlink_core)

add_executable(cipher_client src/client.cpp)
target_link_libraries(cipher_client PRIVATE cipherlink_core)
