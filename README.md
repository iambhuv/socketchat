# Socket Chat

A multi-user chat application written in C with Raylib for the interface.

The project currently opens a blank, resizable window and contains the binary
protocol module that the future client and relay will share. We will build the
UI and networking in small, reusable modules as the project grows.

## Build

You need CMake 3.20 or newer and a C compiler. CMake uses an installed copy of
Raylib 6.0 when available. Otherwise, it downloads and builds Raylib 6.0 from
the official source.

### macOS and Linux

```sh
cmake -S . -B build
cmake --build build
./build/socket_chat
```

### Windows

Run these commands in PowerShell from the project directory:

```powershell
cmake -S . -B build
cmake --build build --config Debug
.\build\Debug\socket_chat.exe
```

When using a single-configuration generator such as MinGW Makefiles, the program
is created at `build\socket_chat.exe` instead.
