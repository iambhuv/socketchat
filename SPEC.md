# Socket Chat: Spec

One-to-one chat over TCP. A Raylib desktop client talks to a small headless
server. Your account is a keypair. Your contact link is your public key.
Every message is signed by its sender.

This file is the contract. If something is not in here, we are not building it.

## What the demo shows

1. Start `chatd` on one laptop (or the VPS).
2. Start `chat` on two laptops. Each picks a nickname and the server address.
3. One copies their contact link, the other pastes it and gives it a nickname.
4. They send messages. Messages appear on the other side and survive restart.

That is the whole product.

## Not building (on purpose)

- Group chats, usernames on the server, offline delivery queues.
- Transport encryption. The server reads messages in plaintext. If asked:
  "we sign messages so nobody can forge a sender; encrypting the wire is the
  next step and would use X25519 + an AEAD."
- Server identity. The client does not verify who the server is. Same answer.
- Read receipts, typing indicators, file transfer, emoji picker.
- Package managers. No vcpkg, no CMake, no Homebrew.

## Programs

| Binary  | What it is                      |
|---------|---------------------------------|
| `chat`  | Raylib GUI client               |
| `chatd` | Headless TCP server, `select()` |
| `nob`   | The build script, in C          |

Everything we write stays under 2500 lines. If a change pushes past that,
something else gets deleted first.

## Dependencies (all vendored in `thirdparty/`, nothing to install)

| Library         | Why                                   | How we use it                                 |
|-----------------|---------------------------------------|-----------------------------------------------|
| `nob.h`         | Build script, dynamic arrays, logging | `nob.c` builds everything. Code uses `da_append`, `String_Builder`, `String_View`, `temp_sprintf`, `nob_log`. |
| raylib (source) | Window, drawing, text input, clipboard| `nob` compiles six raylib modules once into `build/libraylib.a`. HiDPI on, fonts drawn from a 64px atlas with mipmaps. |
| `sqlite3.c`     | Client storage                        | One amalgamation file, compiled once into `build/sqlite3.o`. |
| `monocypher.c`  | Ed25519 sign and verify               | One `.c` + one `.h`. Replaces libsodium.      |

Random bytes come from the OS: `arc4random_buf` (macOS, BSD), `getrandom`
(Linux), `BCryptGenRandom` (Windows). That and `net.h` are the only places
with platform `#ifdef`s.

Build on any machine with a C compiler:

```
cc -o nob nob.c        # once
./nob                  # builds build/chat and build/chatd
./nob test             # builds and runs build/test
./nob run              # builds and starts the client
./nob server [port]    # builds and starts the server
```

On Windows with MSVC: `cl nob.c` then `nob.exe`. Untested so far.

## Layout

```
nob.c                 build script
test.c                the tests
SPEC.md               this file
README.md             how to run the demo, nothing else
assets/fonts/         Nunito, bundled into the binary by nob
thirdparty/           nob.h  monocypher.c/.h  sqlite3.c/.h  raylib/
src/
  protocol.h          frames and messages on the wire
  crypto.h            keypair, sign, verify, random, contact links
  net.h               connect/listen/accept/send/recv/wait
  client.c            client side of the protocol, no UI
  chatd.c             the server
  db.c                SQLite: identity, contacts, messages, settings
  ui.c                button, text box, panel, wrapped text, theme
  chat.c              main + the two screens
```

Unity build: each binary is one translation unit. `chat.c` includes
`client.c`, `db.c` and `ui.c`. `test.c` includes `chatd.c` and `client.c`.
The `.h` files hold definitions, not just declarations; the include guards
handle repeats. Include `nob.h` first.

## Protocol

TCP. Every frame is:

```
u32 length   (big endian, counts the type byte and the payload)
u8  type
... payload
```

Max frame 64 KiB. Anything bigger or with an unknown type closes the socket.

| Type | Name      | Direction | Payload                                            |
|------|-----------|-----------|----------------------------------------------------|
| 1    | HELLO     | C -> S    | `pubkey[32]`                                       |
| 2    | CHALLENGE | S -> C    | `nonce[32]`                                        |
| 3    | AUTH      | C -> S    | `sig[64]` = sign(nonce)                            |
| 4    | MSG       | both      | `from[32] to[32] u64 timestamp_ms u32 len text[len] sig[64]` |
| 5    | OK        | S -> C    | empty                                              |
| 6    | ERROR     | S -> C    | `text`                                             |

`MSG.sig` signs every payload byte before it: `from ‖ to ‖ timestamp ‖ len ‖ text`.

Login:

```
C: HELLO pubkey
S: CHALLENGE nonce
C: AUTH sign(nonce)
S: OK                 (or ERROR + close)
```

Sending: client sends MSG. Server checks `from` equals the logged-in key and
the signature verifies. If `to` is online, the server forwards the payload
unchanged and replies OK. If not, it replies `ERROR "offline"`. The client
verifies the signature again on receipt and stores the message.

Sending to yourself works and is the "Saved messages" chat. The server echoes
it back; the client ignores the echo and stores the message when OK arrives.

A key can be logged in once. A second login with the same key is refused.

## Storage (client only, `~/.socketchat/chat.db`, `--db path` to override)

```sql
identity (pub BLOB, sec BLOB)                       -- one row
contacts (pub BLOB PRIMARY KEY, nickname TEXT)
messages (id INTEGER PRIMARY KEY, contact BLOB, incoming INT,
          timestamp_ms INT, text TEXT)
settings (key TEXT PRIMARY KEY, value TEXT)         -- nickname, dark, last_chat
```

The server has no database and no identity. Restarting it loses nothing.

## Client screens

1. **Setup**: nickname and a Connect button. The server address is not
   asked for; it is baked in at build time from `build/server.h`, which
   `nob` generates once with `127.0.0.1:5050` and you edit for the VPS.
   The nickname is remembered and the client logs in by itself next time.
2. **Chats**: left sidebar lists You (saved messages) and contacts. Right side
   shows the open chat and a one-line composer, Enter sends. Sidebar buttons:
   Copy my link, Add contact, dark mode. Add contact asks for a link and a
   nickname. A message from an unknown key adds that key as a contact.
   The last open chat is reopened on the next login.

Contact link format: `chat://` + 64 hex chars of the public key.

## Design language (`ui.c`)

One look everywhere, so nobody has to make a design decision per screen:

- **Type**: Inter. Title 24, heading 18, body 15, small 13, caption 12.
  Semibold for names and buttons, regular for everything else.
- **Spacing**: multiples of 8. Controls are 40 tall. Page padding 16.
- **Shape**: radius 8 on controls, 12 on cards and bubbles. Flat surfaces
  with a 1px border. No shadows, no glow, no scale animations.
- **Color**: one neutral ramp (bg, surface, surface2, border, text, muted),
  one accent (indigo) with a hover shade and a soft tint, and ok/warn/err.
  Light and dark are the same twelve names with different values.
- **Messages**: incoming on surface2 with a border, outgoing on the accent
  in white, time in the corner, max 60% of the width.

## Code rules

- Functions return `bool`. On failure, log with `nob_log(ERROR, ...)` at the
  point of failure and `return false`. Callers just `if (!f()) return false;`.
  No per-module status enums, no `_status_string` functions.
- Zero-init with `= {0}`. Dynamic arrays are `{items, count, capacity}`.
- No `NULL` checks on parameters we control. `assert` for programmer errors.
- One text box implementation, one button implementation, in `ui.c`.
- No feature flags, no "for later" fields, no unused frame types.
- A `TODO:` comment is fine. A half-built feature is not.

Borrowed from tsoding's code (see musializer `plug.c`, tatr `tatr.c`):

- All client state lives in one `Chat` struct, passed explicitly to every
  screen function. The only file-scope state is the `ui` toolkit struct in
  `ui.c`, which is the widgets' own bookkeeping, like raylib's.
- Widgets identify themselves by call site: `ui_button(bounds, ...)` expands
  to `UI_ID` which hashes `__FILE__` and `__LINE__`. One `active_id` says
  which widget owns the mouse, one `focus_id` which text box has the keyboard.
- Short-lived strings (labels, paths, toasts) come from `temp_sprintf`.
  `temp_reset()` runs once at the top of every frame.
- Every enum that drives a table gets a `COUNT_` last member and a
  `static_assert` next to the table.
- `nob.c` writes `build/build.h` with `GIT_HASH`, shown on the setup screen,
  `build/bundle.h` with the fonts, so the binary needs no files beside it,
  and `build/server.h` once, holding the server address.

## Tests (`./nob test`)

`test.c`, four tests, no framework:

- **frames**: encode two frames, parse both, partial input returns 0, bad
  length and bad type return -1.
- **messages**: build a signed MSG, parse it, verify; flip a bit, verify fails.
- **links**: link round trip, whitespace tolerated, wrong prefix rejected.
- **end_to_end**: a real server and two real clients in one process. Login,
  deliver, self-chat echo, offline recipient, duplicate login refused, peer
  disconnect noticed.

## Who owns what

| File           | Owner | Can explain in 2 minutes                       |
|----------------|-------|------------------------------------------------|
| `protocol.h`   |       | the frame table above                          |
| `net.h`        |       | sockets, `select`, why `send` can be partial   |
| `crypto.h`     |       | keypair, signature, why we sign                |
| `client.c`     |       | the login state machine, polling without blocking |
| `chatd.c`      |       | accept loop, login, forwarding                 |
| `db.c`         |       | the four tables                                |
| `ui.c`         |       | immediate-mode UI, one frame at a time         |
| `chat.c`       |       | screens and how they call the rest             |
| `nob.c`        |       | "the build script is also C"                   |

The previous CMake/libsodium codebase lives in `.archive/`, ignored by git.
