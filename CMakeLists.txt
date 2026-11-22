cmake_minimum_required(VERSION 3.20)

project(CipherAPI VERSION 0.7 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenSSL REQUIRED COMPONENTS Crypto SSL)

set(CIPHERAPI_INCLUDE_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

add_library(cipher_core STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/library.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/profile.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/auth.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/image.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/video.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/commands.cpp"
)

target_include_directories(cipher_core PUBLIC ${CIPHERAPI_INCLUDE_DIRS})
target_link_libraries(cipher_core PUBLIC OpenSSL::Crypto OpenSSL::SSL)

add_executable(cipher_client "${CMAKE_CURRENT_SOURCE_DIR}/src/client.cpp")
target_link_libraries(cipher_client PRIVATE cipher_core)

add_executable(cipher_server "${CMAKE_CURRENT_SOURCE_DIR}/src/server.cpp")
target_link_libraries(cipher_server PRIVATE cipher_core)


# Python SDK shared library
set_target_properties(cipher_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(cipherapi SHARED src/api.cpp)
target_include_directories(cipherapi PUBLIC ${CIPHERAPI_INCLUDE_DIRS})
target_link_libraries(cipherapi PRIVATE cipher_core)


# Authentication tests
enable_testing()
add_executable(cipher_auth_test cipher_auth_test.cpp)
target_link_libraries(cipher_auth_test PRIVATE cipher_core)
add_test(NAME cipher_auth_test COMMAND cipher_auth_test)

# Transport tests
add_executable(cipher_transport_test tests/transport_test.cpp)
target_link_libraries(cipher_transport_test PRIVATE cipher_core)
add_test(NAME cipher_transport_test COMMAND cipher_transport_test)

