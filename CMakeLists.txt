cmake_minimum_required(VERSION 3.20)

project(CipherAPI VERSION 0.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CIPHERAPI_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(cipher_core STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/library.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/profile.cpp"
)
target_include_directories(cipher_core PUBLIC ${CIPHERAPI_INCLUDE_DIRS})

add_executable(cipher_client "${CMAKE_CURRENT_SOURCE_DIR}/src/client.cpp")
target_include_directories(cipher_client PRIVATE ${CIPHERAPI_INCLUDE_DIRS})
target_link_libraries(cipher_client PRIVATE cipher_core)

add_executable(cipher_server "${CMAKE_CURRENT_SOURCE_DIR}/src/server.cpp")
target_include_directories(cipher_server PRIVATE ${CIPHERAPI_INCLUDE_DIRS})
target_link_libraries(cipher_server PRIVATE cipher_core)

