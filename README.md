CipherAPI

<p align="center">
  **A modular C++20 TCP communication system.**
  <br>
  Custom encoding libraries · Binary image transfer · CVS/1 video protocol
</p>---

Overview

CipherAPI is a C++20 communication project built around native POSIX TCP sockets.

It provides a client-server communication system supporting encoded text messages, fast text transmission, binary image transfer, and a custom chunk-based video transfer protocol called CVS/1.

The project explores custom data transformation, protocol design, binary transport, integrity verification, and modular C++ development.

Features

- [x] Modern C++20 codebase
- [x] CMake build system
- [x] Native POSIX TCP sockets
- [x] Full-duplex client-server communication
- [x] Custom text encoding system
- [x] Ten rotating character libraries
- [x] Fast raw-text transmission mode
- [x] Binary image transmission
- [x] SHA-256 image integrity verification
- [x] Raw image transmission without SHA-256
- [x] CVS/1 chunk-based video transmission
- [x] CipherShuffle bit rearrangement
- [x] Optional SHA-256 video integrity verification
- [x] File reconstruction on the receiving side
- [x] Logging and transfer statistics

CVS/1 — Cipher Video Streaming Protocol

CVS/1 is CipherAPI's custom video file transfer protocol.

Instead of transmitting an entire video as one payload, CVS/1 processes it in chunks.

Transfer pipeline

1. Read the source video in chunks of up to 4096 bytes.
2. Convert each chunk into a bitstream.
3. Apply the custom CipherShuffle bit rearrangement.
4. Encode the transformed data using the ten character libraries.
5. Transmit the encoded chunk over TCP.
6. The receiver decodes the data and restores the original bit order.
7. Reconstruct the original video file.
8. Optionally verify the completed file using SHA-256.

Protocol capabilities

Feature| Implementation
Protocol version| CVS/1
Chunk size| 4096 bytes
Transport| TCP
Bit transformation| CipherShuffle
Encoding| Ten custom libraries
Integrity| Optional SHA-256
Reconstruction| Receiver-side

Important: CipherShuffle is a data transformation and obfuscation technique, not encryption. The protocol does not currently provide confidentiality or authenticated encryption.

Image Transfer

CipherAPI supports two image transmission modes:

- Standard image transfer with SHA-256 verification.
- Raw image transfer without SHA-256 verification.

Requirements

- C++20 compatible compiler
- CMake 3.20+
- OpenSSL development libraries
- POSIX-compatible operating system
- Linux or Termux environment

No Boost.Asio dependency is required.

Installation

Clone the repository:

git clone https://github.com/sizzly-k3vin/CipherAPI.git
cd CipherAPI

Configure:

cmake -S . -B build

Compile:

cmake --build build -j$(nproc)

Running

1. Start the server

./build/cipher_server

2. Start the client

In another terminal:

./build/cipher_client

To connect to a different server address:

./build/cipher_client SERVER_IP

The default client address is "127.0.0.1", and the server listens on port "8080".

Available Commands

Command| Description
"/help"| Display available commands
"/stats"| Display communication statistics
"/clear"| Clear terminal output
"/fast"| Toggle fast raw-text mode
"/sendimg FILE"| Send an image with SHA-256
"/sendimg -f FILE"| Send an image without SHA-256
"/sendvid FILE"| Send a video using CVS/1 with SHA-256
"/sendvid -f FILE"| Send a video using CVS/1 without SHA-256
"/quit"| Close the connection

Architecture

                    CipherAPI
                        |
                  TCP Transport
                        |
              +---------+---------+
              |                   |
            Client              Server
              |                   |
       +------+-------+     +-----+------+
       |      |       |     |     |      |
      Text  Image   Video  Text Image  Video
       |      |       |     |     |      |
       +------+-------+     +-----+------+
              |                   |
        Custom Encoding      Reconstruction
              |                   |
              +-------- TCP ------+

Technology Stack

Component| Technology
Language| C++20
Build system| CMake
Networking| POSIX sockets
Transport protocol| TCP
Cryptographic hashing| OpenSSL SHA-256
Video protocol| CVS/1
Video transformation| CipherShuffle
Environment| Linux / Termux

Project Structure

CipherAPI/
├── CMakeLists.txt
├── README.md
├── LICENSE
├── include/
│   ├── debug.hpp
│   ├── image.hpp
│   ├── library.hpp
│   ├── logger.hpp
│   ├── neon.hpp
│   ├── profile.hpp
│   ├── transport.hpp
│   └── video.hpp
├── src/
│   ├── client.cpp
│   ├── server.cpp
│   ├── image.cpp
│   ├── library.cpp
│   ├── profile.cpp
│   └── video.cpp
├── libraries/
│   ├── lib01.txt
│   ├── lib02.txt
│   └── ...
│
└── tests/
    └── library_test.cpp

Development Roadmap

Potential future development:

- [ ] Authenticated encryption
- [ ] Client authentication
- [ ] Improved transfer recovery
- [ ] Transfer progress reporting
- [ ] More comprehensive protocol testing
- [ ] Expanded profile exchange functionality
- [ ] Performance optimization

Contributing

Contributions, suggestions, and improvements are welcome.

When submitting changes:

- Use descriptive commit messages.
- Keep protocol changes documented.
- Test both client and server.
- Use version tags for significant releases.

License

CipherAPI is licensed under the MIT License.

See ""LICENSE"" (LICENSE) for the full license text.

---

<p align="center">
  **CipherAPI — Built from bytes, one protocol at a time.**
</p>

## Authentication and Device Pairing

CipherAPI is designed to use per-device authentication keys and server-managed device records.

### Identity separation
- **Server account UUID:** Internal identifier used only by the server to group authorized devices. It is never the client display UserID.
- **Device key:** A unique, cryptographically random 256-bit key generated by the server for each device.
- **Client UserID:** A locally chosen display identifier. It is not sent as an authentication credential and does not determine permissions or identity on the server.

### First-device registration
1. The server creates an internal account record.
2. The server generates a unique device key.
3. The client securely receives and stores the key.
4. The server stores a verifier rather than the raw key.

### New-device pairing
1. The new device requests pairing.
2. The server creates a short-lived, single-use pairing code and challenge.
3. An existing authorized device approves the request using cryptographic proof.
4. The server verifies the approval and consumes the pairing request.
5. The server generates a separate device key for the new device.

### Security requirements
- Authenticate before accepting application messages or media.
- Use TLS for transport encryption and initial key delivery.
- Use fresh nonces and HMAC-SHA256 for challenge-response authentication.
- Expire pairing requests and prevent replay.
- Allow individual device revocation.
- Never use the client UserID as an authentication credential.

**Implementation status:** Design specification. These requirements are not yet implemented by this documentation update.
