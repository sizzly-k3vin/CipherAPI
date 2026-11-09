from pathlib import Path
import shutil

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"
INCLUDE = ROOT / "include"
CMAKE = ROOT / "CMakeLists.txt"

if not SRC.is_dir():
    raise SystemExit("ERROR: src/ directory not found")

# Discover C++ files
sources = sorted(SRC.rglob("*.cpp"))

# Known executable entry points
executables = {
    "client": SRC / "client.cpp",
    "server": SRC / "server.cpp",
}

for name, path in executables.items():
    if not path.is_file():
        raise SystemExit(f"ERROR: Missing executable source: {path}")

# Remaining source files become the shared library
core_sources = [
    p for p in sources
    if p not in executables.values()
]

# Discover directories containing headers
include_dirs = []
if INCLUDE.is_dir():
    include_dirs = sorted({
        p.parent for p in INCLUDE.rglob("*")
        if p.is_file() and p.suffix in {".h", ".hpp"}
    } | {INCLUDE})

def cmake_path(path):
    return "${CMAKE_CURRENT_SOURCE_DIR}/" + path.relative_to(ROOT).as_posix()

lines = [
    "cmake_minimum_required(VERSION 3.20)",
    "",
    "project(CipherAPI VERSION 0.4 LANGUAGES CXX)",
    "",
    "set(CMAKE_CXX_STANDARD 20)",
    "set(CMAKE_CXX_STANDARD_REQUIRED ON)",
    "set(CMAKE_CXX_EXTENSIONS OFF)",
    "",
]

if include_dirs:
    lines.append("set(CIPHERAPI_INCLUDE_DIRS")
    for directory in include_dirs:
        lines.append(f'    "{cmake_path(directory)}"')
    lines.extend([")", ""])

if core_sources:
    lines.append("add_library(cipher_core STATIC")
    for source in core_sources:
        lines.append(f'    "{cmake_path(source)}"')
    lines.extend([
        ")",
        "target_include_directories(cipher_core PUBLIC ${CIPHERAPI_INCLUDE_DIRS})",
        "",
    ])

for name, source in executables.items():
    target = f"cipher_{name}"
    lines.append(f'add_executable({target} "{cmake_path(source)}")')

    if include_dirs:
        lines.append(
            f"target_include_directories({target} PRIVATE ${'{'}CIPHERAPI_INCLUDE_DIRS{'}'})"
        )

    if core_sources:
        lines.append(f"target_link_libraries({target} PRIVATE cipher_core)")

    lines.append("")

# Back up existing CMake configuration
if CMAKE.exists():
    shutil.copy2(CMAKE, ROOT / "CMakeLists.txt.bak")

CMAKE.write_text("\n".join(lines) + "\n")

print("CipherAPI CMake configuration generated!")
print(f"Discovered C++ files: {len(sources)}")
print(f"Core library sources: {len(core_sources)}")
print(f"Header directories: {len(include_dirs)}")
print("Executables: cipher_client, cipher_server")
print("Generated: CMakeLists.txt")
