CipherAPI

<p align="center">
  **A lightweight C++ TCP communication project.**Built with C++20, CMake, and native POSIX sockets.

</p>---

Overview

CipherAPI is a lightweight networking project written in modern C++20.

The current prototype establishes TCP communication between a client and server using native POSIX sockets, without relying on Boost.Asio or other networking frameworks.

The goal is to develop a modular communication system that can eventually support profile exchange and encryption.

Current Features

- [x] C++20 codebase
- [x] CMake build system
- [x] Native POSIX socket implementation
- [x] TCP server and client
- [x] Client-to-server message transmission
- [x] Server acknowledgment
- [x] Server startup banner and logging
- [ ] Profile exchange
- [ ] End-to-end encryption
- [ ] Authentication system

Requirements

- C++20 compatible compiler
- CMake
- POSIX-compatible operating system
- Linux or Termux environment

No Boost dependency is required.

Installation

Clone the repository:

git clone <YOUR_REPOSITORY_URL>
cd CipherAPI

Configure the project:

cmake -S . -B build

Compile:

cmake --build build -j$(nproc)

Running

Open your first terminal and start the server:

./build/cipher_server

The server listens on:

127.0.0.1:8080

Open another terminal and launch the client:

./build/cipher_client

The client connects to the server, transmits a message, and receives an acknowledgment.

Architecture

       CipherAPI
           |
     TCP Connection
           |
     +-----+-----+
     |           |
   Client      Server
     |           |
   Send ------> Receive
     |           |
     |        Logging
     |           |
   Receive <--- ACK

Technology Stack

Component| Technology
Language| C++20
Build system| CMake
Networking| POSIX sockets
Protocol| TCP
Environment| Linux / Termux

Development Roadmap

The project is intended to evolve through multiple development stages:

- TCP Foundation: Establish reliable client-server communication.
- Profile Engine: Develop profile-related functionality.
- Encryption Layer: Introduce secure communication.
- Protocol Expansion: Extend the communication architecture.

These are development goals, not claims about currently available functionality.

Contributing

Contributions, suggestions, and improvements are welcome.

When submitting changes, use descriptive commit messages and version tags to keep development history organized.

License

No license has been specified yet.
