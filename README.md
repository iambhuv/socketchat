# Socket Chat

One-to-one chat over TCP. A Raylib desktop client (`chat`) and a small
headless server (`chatd`). Your account is a keypair, your contact link is
your public key, and every message is signed. See [SPEC.md](SPEC.md) for
what is and is not being built.

## Build

You need a C compiler. Nothing else.

```
cc -o nob nob.c        # once. On Windows with Visual Studio: cl nob.c
./nob                  # builds build/chat and build/chatd
./nob test             # runs the tests
./nob ui-test          # headless UI layout regression checks
./nob release          # the build to hand out: optimised and stripped
./nob linux            # cross-compiles chatd for a Linux server (needs zig)
```

The first build compiles raylib and SQLite from `thirdparty/`, which takes
about a minute. After that builds take a second or two.

On Linux, raylib needs the X11 development headers:

```
sudo apt install libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev libgl-dev
```

## Point the client at your server

The first `./nob` writes `build/server.h`:

```c
#define SERVER "127.0.0.1:5050"
```

Change it to the server's address (`192.168.1.20` or `host:port`), run
`./nob` again, and hand out `build/chat`. The client never asks for a server.

## Run the demo

1. Start the server on one machine: `./build/chatd` (port 5050 by default).
2. Start the client on two machines: `./build/chat`. Pick a nickname and a
   passphrase. The passphrase encrypts your identity and history on disk and
   there is no reset, so do not forget it.
3. In one client press **Copy my link**. In the other press **Add contact**,
   paste it, give it a name.
4. Chat. Messages are kept in `~/.socketchat/chat.db`; the server keeps nothing.

Run two clients on one machine with separate databases:

```
./build/chat --db alice.db
./build/chat --db bob.db
```
