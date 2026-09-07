# Socket Chat

A multi-user chat application written in C with Raylib for the interface.

The project currently opens a blank, resizable window and contains binary
protocol and cross-platform TCP modules that the future client and relay will
share. We will build the UI and networking in small, reusable modules as the
project grows.

## Build

You need CMake 3.20 or newer, a C compiler, libsodium, and SQLite. CMake uses an
installed copy of Raylib 6.0 when available. Otherwise, it downloads and builds
Raylib 6.0 from the official source.

### macOS

Install the libraries with Homebrew:

```sh
brew install libsodium sqlite
```

Then configure CMake with their Homebrew locations:

```sh
cmake -S . -B build \
  -DCMAKE_PREFIX_PATH="$(brew --prefix libsodium);$(brew --prefix sqlite)"
cmake --build build
./build/socket_chat
```

### Linux (Ubuntu or Debian)

```sh
sudo apt install build-essential cmake libsodium-dev libsqlite3-dev
cmake -S . -B build
cmake --build build
./build/socket_chat
```

### Windows

Install Visual Studio with the **Desktop development with C++** workload, CMake,
Git, and [vcpkg](https://learn.microsoft.com/vcpkg/get_started/get-started).
Set `VCPKG_ROOT` to your vcpkg directory, then run these commands in PowerShell
from the project directory:

```powershell
cmake -S . -B build `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Debug
.\build\Debug\socket_chat.exe
```

The included `vcpkg.json` tells vcpkg to install libsodium and SQLite during the
CMake configure step. When using a single-configuration generator such as
MinGW Makefiles, the program is created at `build\socket_chat.exe` instead.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

The dependency test checks that libsodium can generate random bytes and SQLite
can create a small in-memory database. It does not create a file on your disk.
The networking test opens a temporary connection on your own computer and sends
a small message in both directions.
