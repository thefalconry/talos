# Talos

Talos is a desktop and mobile client for [Talon](https://github.com/thefalconry/talon),
written in Qt 6 (C++ core, Qt Quick interface). One codebase for Linux,
Windows, macOS and Android.

It speaks Talon's Client Bridge Protocol (HTTP + Server-Sent Events) to a
daemon running the `native` frontend. It is a new client, written against the
daemon's protocol definition rather than ported from the Flutter companion.

![A conversation, light theme](docs/screenshots/chat.png)

| Dark | Phone | Mutual TLS |
| --- | --- | --- |
| ![Dark theme](docs/screenshots/chat-dark.png) | ![Phone layout](docs/screenshots/phone.png) | ![Connection settings with a client certificate](docs/screenshots/connection-mtls.png) |

(Screenshots are of the app running against the bundled mock bridge.)

## What it does

**Chat**

- Chats grouped by day, filter by title, unread markers, rename, delete, reset
- Live replies: streamed text, the model's reasoning, and each tool call with
  its input, output, error and duration
- Markdown with fenced code blocks (copy button, syntax colouring), tables,
  images shown inline, file attachments
- Attach any files (picker, drag and drop, paste an image); uploads stream
  with progress
- Slash commands: completion from the daemon's own list, with each
  command's arguments. On a daemon too old to have one, the app carries out
  `/stop`, `/reset`, `/model`, `/effort` and `/pulse` itself
- Search every chat (Ctrl+K), or only the open one (Ctrl+F)
- Pin chats to the top, archive the ones in the way, save a conversation
  to a file, edit a message and send it again
- Formulas written in LaTeX are shown as readable text (`α² + 1/n`, not
  typeset), and Mermaid flowcharts are drawn
- Showing a reply never contacts another server: an image in a reply
  becomes a link, HTML is shown as text, and the window's own network
  access is switched off. Attachments that are programs or scripts are
  saved, not opened
- Queue a follow-up while a turn runs, edit or cancel it from any device
- Stop a running turn
- Per-chat model, reasoning effort and backend
- Context-window meter with a reset
- Older history pages in as you scroll up
- `Ctrl+K`: run an action, jump to a chat, full-text search of every message
- Copy a conversation as Markdown
- Instant cold start from an offline snapshot

**Connection**

- *This computer*: finds a local Talon through its discovery file, optionally
  starting the daemon when it isn't running
- *Another machine*: address + token, HTTP or HTTPS
- **Certificate pinning.** A self-signed bridge certificate is shown for you
  to confirm before any token is sent, then pinned; a different certificate
  later is refused
- **Mutual TLS.** Import a `.p12`/`.pfx` client certificate for a bridge
  behind a reverse proxy that demands one. It is presented on every
  connection
- **Bridge links open inside the app.** A link that points at the connected
  server (a file, `/media`, the secret-drop form, a pairing page) is fetched
  over the app's own connection, with the client certificate, the pin and
  the token. A system browser has no client certificate and could not open
  them
- A home-network address that is preferred whenever it answers
- Pairing by link: a `talon://pair` link (pasted, clicked, or passed on the
  command line), or the address of the pairing page `/mesh link` gives you,
  which the app redeems itself
- Trades the shared bridge token for a per-device credential, and rotates it
  when Talon asks
- Several saved connections; credentials live in the system keyring
- With the app lock on, tokens and client certificates are sealed with
  the passcode as well: after a restart Talos stays offline until unlocked
- Reconnects on its own: backoff, a dead-stream timeout, network changes,
  resume from sleep, and daemon restarts
- Fallback addresses: when the daemon advertises other ways to reach it
  (`native.endpoints`: a second name, or the same name at a fixed IP for
  when DNS is down), Talos moves to the next one that answers and returns
  to the saved address once it is back. A fallback is only used if it shows
  the certificate already pinned

**On the desktop**

- A small quick-ask window from anywhere: `talos --quick`, or a system-wide
  shortcut where the desktop offers apps one (KDE Plasma, GNOME 48+) and on
  macOS, where it is Control-Option-Space
- Notifications open the chat they are about, and on desktops with inline
  replies (and in macOS's Notification Centre) you can answer from the
  notification
- On a Mac: the menu bar, ⌘ shortcuts, a menu bar icon to keep running
  behind, and the name the Mac goes by in Sharing as the device's name
- The interface in English, Spanish and Hindi; screen-reader names and
  keyboard access on the controls

**Talon itself**

- Read and change the daemon's settings (name, default model, timezone,
  heartbeat, pulse, dream), see its health, restart it, run a dream
- Turn plugins and skills on and off
- Browse what Talon remembers, and why
- Daemon log viewer with level filter, text filter and follow

**Mesh device**

- Registers this device, answers `ring`, `status` and `locate`
- With device control granted for that server: `exec`, `read_file`,
  `write_file`, `list_dir`, `stat`, `delete`, `mkdir`, `move`, and streamed
  `upload_file` / `download_file` with SHA-256 checks
- Device control can be narrowed: shell commands, reading files and
  changing files each have a switch, and file commands can be confined to
  folders you list
- Optional approval prompt before every command, command limits, and an
  audit trail of everything run
- **Android**: share text or files to Talos from other apps; an optional
  background service keeps the connection up with the app closed, starts
  at boot and announces replies
- **Background service** (Linux, macOS): `talos --headless` keeps the device
  on the mesh with the app closed; Settings or `talos service install` sets
  it up as a systemd user unit, or on a Mac as a LaunchAgent that runs
  while you are signed in
- **`update_node`**: Talon can push a new Talos build. It is installed only
  if it matches the SHA-256 Talon names, is a Talos build (a talon-node
  binary is refused) and is not older than the running one
- **`computer`** (Linux, macOS; experimental): screenshot, pointer and
  keyboard. On Linux through the desktop's own remote-control portal, which
  asks you first and shows when a session runs. On macOS with the system's
  Accessibility and Screen Recording permissions, which only you can grant;
  there it also reads what is on screen (`snapshot`) and reports what a
  click hit

**The app**

- Light and dark themes, accent colour, text size
- Notifications, tray, launch at sign-in
- App lock: a passcode (Argon2id) that covers the window and encrypts the
  cached chats (XChaCha20-Poly1305), with fingerprint unlock on Linux
  (fprintd), Touch ID on macOS, and fingerprint or face on Android (a
  keystore key released by the system's biometric prompt)
- Read replies aloud; hands-free voice mode on Android
- Update check against a GitHub repository's releases, with checksum
  verification

## Build

Needs Qt 6.8 or newer (developed on 6.11) with the Qt Quick, Multimedia,
TextToSpeech and Positioning modules, CMake 3.21+ and a C++20 compiler.

### Linux

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
./build/talos
```

With a Qt from the Qt installer or `aqt` instead of the distribution's, add
`-DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.2/gcc_64` to the first command.

### macOS

With Xcode's command line tools and [Homebrew](https://brew.sh):

```bash
brew install qt cmake ninja
```

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
open build/Talos.app
```

From a terminal the program itself is `build/Talos.app/Contents/MacOS/Talos`
(`--headless`, `--quick`, `service install`). A Qt from the Qt installer or
`aqt` works the same with `-DCMAKE_PREFIX_PATH=$HOME/Qt/6.11.2/macos`.

### Windows

The same three commands from a Qt command prompt, or open `CMakeLists.txt`
in Qt Creator. `windeployqt` collects the Qt libraries for distribution.

### Android

```bash
~/Qt/6.11.2/android_arm64_v8a/bin/qt-cmake -S . -B build-android -G Ninja \
  -DQT_HOST_PATH=$HOME/Qt/6.11.2/gcc_64 \
  -DANDROID_SDK_ROOT=$HOME/Android/Sdk \
  -DANDROID_NDK_ROOT=$HOME/Android/Sdk/ndk/27.2.12479018 \
  -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build-android
```

The APK lands under `build-android/android-build/build/outputs/apk/`.

### Packages

```bash
packaging/fedora/build-rpm.sh
```

builds `dist/talos-<version>-1.<dist>.x86_64.rpm` from the committed tree.
It depends on Fedora's own Qt 6 packages rather than bundling Qt.

```bash
packaging/android/build-apk.sh
```

builds and signs `dist/talos-<version>-arm64.apk`. It signs with the keystore
named by `TALOS_KEYSTORE` (see the script); an APK can only be updated by
one signed with the same key, so keep that file.

```bash
packaging/macos/build-dmg.sh
```

builds `dist/Talos-<version>-<arch>.dmg`: the app with Qt inside it, on a
disk image that opens with the app beside the Applications folder. It runs
on the macOS version its Qt was built for and later (Homebrew's Qt targets
the Mac it is installed on; the Qt Company's builds reach back several
releases). Without a signing identity the app is signed ad hoc: it runs on
the Mac that built it, and on another Mac after being allowed under Privacy
& Security. `TALOS_SIGN_IDENTITY` and `TALOS_NOTARY_PROFILE` (see the
script) sign it with a Developer ID and notarise the image.

### Options

| CMake option | Meaning |
| --- | --- |
| `TALOS_UPDATE_REPO` | GitHub `owner/repo` whose releases the updater watches. Empty by default; it can also be set in Settings |
| `TALOS_BUILD_TESTS` | Build the tests (default on) |

## Connecting

Run Talon with the native bridge:

```jsonc
// ~/.talon/config.json
{ "frontend": "native", "native": { "host": "127.0.0.1", "port": 19880 } }
```

On the same machine choose **This computer**. For another machine set
`"host": "0.0.0.0"` and a `"token"`, choose **Another machine**, and enter
the address and token. Behind a reverse proxy with client certificates
([Talon's guide](https://github.com/thefalconry/talon/blob/main/docs/mtls.md)),
enter the proxy's `https://` address and import the `.p12` under *Client
certificate, pinning and more*.

## Trying it without Talon

`tools/mock-bridge.mjs` stands in for a daemon: every route, sample chats,
and a canned streaming reply.

```bash
node tools/mock-bridge.mjs --port 19880 --token secret
```

Add `--tls` for a self-signed HTTPS bridge, or `--mtls` to also demand a
client certificate (it writes a `client.p12` and prints where). To keep a
trial apart from your real settings and keyring:

```bash
TALOS_HOME=/tmp/talon-trial ./build/talos
```

## Tests

```bash
ctest --test-dir build --output-on-failure
```

The suite covers the pure logic (address parsing, pairing links, the SSE
parser, Markdown blocks, crypto, the wire fixtures) and drives a real
session against the mock bridge over plain HTTP, self-signed TLS and mutual
TLS: credential upgrade, certificate pinning and refusal, in-app bridge
downloads, the secret drop, and every mesh command. The integration half
needs `node`, and `openssl` for the TLS cases; without them those cases are
skipped. On macOS `openssl` is LibreSSL, which cannot write the legacy
certificate bundle one case reads back; with Homebrew's first on `PATH`
(`PATH=/opt/homebrew/bin:$PATH ctest …`) nothing is skipped.

## Layout

```
src/core/    protocol and state, no UI
  BridgeClient   HTTP + SSE transport, TLS pinning, client certificates
  Session        connection lifecycle, chats, messages, live turns
  BridgeLinks    in-app handling of links served by the bridge
  Store          preferences, profiles, snapshot; SecretStore for credentials
src/mesh/    this device on Talon's mesh: commands, transfers, audit
src/app/     app lock, updater, voice, tray and notifications, QML glue
qml/         the interface
tools/       mock bridge
tests/       unit and integration tests, protocol fixtures
```

## What is different from, or missing against, Talon's own clients

- **Voice.** Speech input uses Android's recogniser; desktop has none, so
  voice mode there reads replies and takes typed turns. There is no
  on-device neural voice (the companion's Kokoro model); the system's
  text-to-speech is used.
- **Android background connection.** With "Stay reachable with the app
  closed" on, the connection runs in a service of its own that goes on
  after the app is closed and starts again at boot. It has not been run on
  a device yet. While it is on, tokens are not sealed by the app lock,
  because the service has to read them unattended.
- **`install_apk`** (Android) hands the package to the system installer,
  which asks the person holding the phone to confirm. It is never silent:
  that needs root or Shizuku, which Talos does not use, and `exec` likewise
  runs as the app's own user.
- **`computer`** covers screenshot, move, click, drag, scroll, type and key
  on Linux desktops that have the RemoteDesktop portal, and on macOS.
  `snapshot` (the accessibility tree) is macOS only, as it is in talon-node.
  On Linux the pointer position reported is where Talos last put it, because
  Wayland does not reveal the real one.
- **`update_node`** replaces a copy of Talos the user can write to (a build
  tree, a portable install). A system package is left to its package
  manager, and the capability is then not advertised.
- **Fingerprint unlock on Linux and Touch ID on macOS** are a convenience:
  fprintd or the system confirms the finger and the key is read from the
  login keyring or Keychain, which is what protects it at rest. (A Keychain
  item that itself demands the fingerprint needs an app signed with a
  developer identity.) On Android the device itself releases the key.
- **The background service** keeps credentials in an owner-only file while
  it is installed, as talon-node does, because it runs with no desktop
  session and so no unlocked keyring. Fingerprint unlock is unavailable in
  that mode. On macOS it runs while you are signed in, not from boot.
- **The quick-ask shortcut on macOS** is fixed at Control-Option-Space and
  is refused if another app already holds those keys.
- **Notifications on macOS** open their chat when clicked and take a reply
  only in a build signed with a developer identity: the Notification Centre
  refuses any other app outright. An unsigned build still tells you, by
  posting through the system's script runner, under that program's name
  and without click-through or replies.
- **Updates.** The app downloads and verifies a release, then hands it to
  the system; it does not replace itself in place. No release repository is
  configured by default.
- **Secrets on Android** are kept in the app's private storage rather than
  the hardware keystore (the biometric key is the exception).
- **Inline buttons without a link** are treated as quick replies: clicking
  one sends its label as your message. The bridge has no callback route.

## Build status

- **Linux**: built, tested (the suite above) and run, including against
  Fedora's own Qt 6.11 libraries.
- **Android**: builds a signed arm64 APK. It has not been run on a device,
  so the Android-only code (notifications, battery, ringtone, speech input,
  screen privacy, biometric unlock) is compiled but untried.
- **macOS**: built, tested (the suite above, with a Keychain round trip
  added) and run on Apple silicon with Homebrew's Qt 6.11, and packaged as
  a disk image. Run for real there: the launchd background service
  (install, mesh registration, restart after a kill, removal), screen
  control (screenshot, click, type, shortcuts, drag, scroll, snapshot),
  the menu bar and ⌘ shortcuts, closing to the menu bar and reopening,
  the quick-ask hot key, launch at sign-in, and notifications by the
  fallback below. Not exercised: Touch ID (the Mac had no sensor), and
  notifications through the Notification Centre itself, with their
  click-through and inline reply, which need a signed build. The image is
  signed ad hoc, not notarised.
- **Windows**: on hold. Its platform code (DPAPI secret storage, battery,
  launch at sign-in) is written but has never been through a compiler; the
  background service, screen control and biometric unlock have no backend
  there yet.
- **Not exercised on real hardware or a live desktop**: fingerprint unlock
  (no reader was available) and `computer`'s pointer and keyboard injection
  (it needs a person to accept the desktop's consent dialog). Their pure
  logic is covered by tests.

## Licence

Talos is free software under the GNU General Public License, version 2 only
(`GPL-2.0-only`); see [LICENSE](LICENSE). It links Qt 6, which is available
under the GPL version 2 among other licences. Bundled third-party material
and its terms are listed in [THIRD_PARTY.md](THIRD_PARTY.md).
