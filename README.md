# Socket Chat

A multi-user chat application written in C with Raylib for the interface.

The client currently shows an animated Raylib username setup screen built from
custom widgets. The project also contains binary protocol and cross-platform
TCP modules that the client and server share. Version 1 focuses on private,
one-to-one chats so the code remains practical to explain and maintain.

`socket_chat` is always the Raylib GUI client. The separate
`socket_chat_server` program is the headless server.

The interface uses the rounded [Nunito](https://fonts.google.com/specimen/Nunito)
typeface under the SIL Open Font License included with the font file.

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

The GUI does not ask users for a server address. The development address and
port are kept together in `src/core/app_config.h`; change them there when the
server is deployed somewhere else.

On first launch, the client creates `socket_chat.db` and stores a new Ed25519
identity keypair in it. Later launches load the same identity, so the user's
public key remains stable.

## Tests

```sh
ctest --test-dir build --output-on-failure
```

The dependency test checks that libsodium can generate random bytes and SQLite
can create a small in-memory database. It does not create a file on your disk.
The networking test opens a temporary connection on your own computer and sends
a small message in both directions.

## Basic server

Start the server on its default TCP port, 5050:

```sh
./build/socket_chat_server
```

You can choose another port by passing it as the first argument:

```sh
./build/socket_chat_server 7000
```

On Windows with a Debug build, use `build\Debug\socket_chat_server.exe`.

The server accepts up to 32 clients and validates binary protocol frames. On the
first run it creates `socket_chat_server.db` to keep its Ed25519 signing key
across restarts. For each `HELLO`, it generates a fresh nonce and X25519 keypair,
then returns a signed `CHALLENGE`. It prints its public key at startup for future
client pinning. The server does not yet accept `AUTHENTICATE` or ordinary chat
requests; client verification, encrypted transport, and message storage are the
next stages. Keep the server database private and back it up: replacing it
changes the server's identity.

With the server running, enter a valid username in the Raylib client and press
**Create my account**. The client connects to the configured address and sends a
`HELLO` containing its public identity, a fresh nonce, and a fresh ephemeral
X25519 public key. The client currently shows connection success after sending
`HELLO`; it does not yet verify the server's response. Username registration
begins after authentication is added.
