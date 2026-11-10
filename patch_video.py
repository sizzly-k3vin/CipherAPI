from pathlib import Path
import re

CLIENT = Path("src/client.cpp")
SERVER = Path("src/server.cpp")

sender = r'''
static void transmit_video(int fd, const LibraryManager& libraries,
                           Neon::Stats& stats, const std::string& path,
                           bool use_sha256) {
    const auto start = std::chrono::steady_clock::now();

    auto result = CipherVideo::send_file(
        fd, libraries, path, use_sha256
    );

    const auto end = std::chrono::steady_clock::now();
    const double seconds =
        std::chrono::duration<double>(end - start).count();

    ++stats.sent_messages;
    stats.sent_bytes += result.wire_bytes;

    Neon::info(
        "CVS/1 | " + std::to_string(result.original_bytes) +
        " original bytes | " + std::to_string(result.wire_bytes) +
        " wire bytes | " + std::to_string(result.chunks) +
        " chunks | " + std::to_string(seconds) + " seconds"
    );

    Neon::send(
        use_sha256
            ? "Video transmitted with SHA-256"
            : "Video transmitted without SHA-256"
    );
}

'''

def replace_once(source, pattern, replacement, label):
    updated, count = re.subn(pattern, replacement, source, count=1,
                             flags=re.MULTILINE)
    if count != 1:
        raise RuntimeError(f"{label}: expected one match, found {count}")
    return updated

def patch_file(path, main_pattern, command_fd):
    source = path.read_text()

    if '#include "video.hpp"' in source:
        raise RuntimeError(f"{path}: video.hpp already included; refusing duplicate patch")

    source = replace_once(
        source,
        r'^#include "neon\.hpp"\s*$',
        '#include "neon.hpp"\n#include "video.hpp"',
        f"{path}: include"
    )

    source = replace_once(
        source,
        main_pattern,
        sender + r'\g<0>',
        f"{path}: sender function"
    )

    # Add a persistent receiver object inside the receiving thread.
    thread_pattern = r'^([ \t]*)std::thread receiver\(\[&\]\(\) \{\s*try \{'
    def thread_replacement(match):
        indent = match.group(1)
        return (
            indent + 'std::thread receiver([&]() {\n'
            + indent + '    CipherVideo::Receiver video_receiver(libraries);\n'
            + indent + '    try {'
        )

    source = replace_once(
        source, thread_pattern, thread_replacement,
        f"{path}: receiver object"
    )

    # Add the CVS/1 receive branch before the existing IMAGE branch.
    image_pattern = r'^([ \t]*)else if \(type == CipherTransport::IMAGE\) \{'
    def image_replacement(match):
        indent = match.group(1)
        branch = (
            indent + 'else if (type == CipherTransport::VIDEO_START ||\n'
            + indent + '         type == CipherTransport::VIDEO_DATA ||\n'
            + indent + '         type == CipherTransport::VIDEO_END) {\n'
            + indent + '    if (type == CipherTransport::VIDEO_START)\n'
            + indent + '        Neon::receive("CVS/1 video transfer started");\n\n'
            + indent + '    std::string saved = video_receiver.handle(type, payload);\n\n'
            + indent + '    if (!saved.empty()) {\n'
            + indent + '        ++stats.received_messages;\n'
            + indent + '        Neon::decode("CVS/1 video reconstructed");\n'
            + indent + '        Neon::print_message("VIDEO", "Saved to " + saved);\n'
            + indent + '    }\n'
            + indent + '}\n'
        )
        return branch + match.group(0)

    source = replace_once(
        source, image_pattern, image_replacement,
        f"{path}: video receive branch"
    )

    # Add /sendvid before /sendimg.
    command_pattern = r'^([ \t]*)if \(message\.rfind\("/sendimg ", 0\) == 0\) \{'
    def command_replacement(match):
        indent = match.group(1)
        inner = indent + '    '
        block = (
            indent + 'if (message.rfind("/sendvid ", 0) == 0) {\n'
            + inner + 'std::string argument = message.substr(9);\n'
            + inner + 'bool no_sha = argument.rfind("-f ", 0) == 0;\n'
            + inner + 'std::string path = no_sha ? argument.substr(3) : argument;\n\n'
            + inner + 'if (path.size() >= 2 && path.front() == \'"\' && path.back() == \'"\')\n'
            + inner + '    path = path.substr(1, path.size() - 2);\n\n'
            + inner + 'try {\n'
            + inner + '    transmit_video(' + command_fd + ', libraries, stats, path, !no_sha);\n'
            + inner + '} catch (const std::exception& e) {\n'
            + inner + '    Neon::error(e.what());\n'
            + inner + '}\n'
            + inner + 'continue;\n'
            + indent + '}\n\n'
        )
        return block + match.group(0)

    source = replace_once(
        source, command_pattern, command_replacement,
        f"{path}: sendvid command"
    )

    return source

def main():
    if "video.cpp" not in Path("CMakeLists.txt").read_text():
        raise RuntimeError("CMakeLists.txt does not include src/video.cpp")

    transport = Path("include/transport.hpp").read_text()
    for frame in ("VIDEO_START", "VIDEO_DATA", "VIDEO_END"):
        if frame not in transport:
            raise RuntimeError(f"Transport is missing {frame}")

    client_original = CLIENT.read_text()
    server_original = SERVER.read_text()

    # Build both patched versions in memory before writing either.
    client_new = patch_file(
        CLIENT,
        r'^int main\(int argc, char\* argv\[\]\) \{',
        "sock"
    )
    server_new = patch_file(
        SERVER,
        r'^int main\(\) \{',
        "client"
    )

    # Backups, then writes.
    CLIENT.with_suffix(".cpp.pre_video.bak").write_text(client_original)
    SERVER.with_suffix(".cpp.pre_video.bak").write_text(server_original)

    CLIENT.write_text(client_new)
    SERVER.write_text(server_new)

    print("CVS/1 integration patch applied successfully.")
    print("Backups: src/client.cpp.pre_video.bak")
    print("         src/server.cpp.pre_video.bak")

if __name__ == "__main__":
    main()
