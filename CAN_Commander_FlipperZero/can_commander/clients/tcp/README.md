# CAN Commander TCP Client

This standalone terminal client connects to the CAN Commander Wi-Fi CLI over
raw TCP. It is separate from the Flipper app build and uses CMake plus C++17,
with native sockets on Windows and POSIX platforms.

## Build

Run these commands from the Flipper project root:

```sh
cmake -S clients/tcp -B build/tcp
cmake --build build/tcp
```

For cross-compilation, provide the toolchain file for the target to CMake:

```sh
cmake -S clients/tcp -B build/tcp-arm \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/toolchain.cmake
cmake --build build/tcp-arm
```

## Connect

```sh
./build/tcp/cancommander-tcp 192.168.4.1
```

On Windows, run `build\tcp\Debug\cancommander-tcp.exe` (or
`build\tcp\Release\cancommander-tcp.exe` for a Release build).

The port defaults to `7777`; it can be specified as the second argument.
Once connected, enter commands supported by the board's TCP CLI. On POSIX
systems, press `Ctrl+D` to close the client's send side; on Windows, press
`Ctrl+Z`. The server's output continues to be displayed until it closes the
connection.

The TCP CLI has the same privileges as the board's other CLI transports,
including CAN transmission. Use it only with systems you own or are authorized
to test, and prefer a bench setup when experimenting.

## Guided passive capture

The guided mode starts only `tool start read_all bus=can0 ascii=0`; it never
sends CAN frames. It refuses to replace an already-running tool. Use a bench
setup or a stationary vehicle secured in Park with the parking brake set,
and make sure a CAN transceiver is fitted if you expect to receive frames.

```sh
./build/tcp/cancommander-tcp capture 192.168.4.1 vehicle-session.jsonl
```

The port defaults to `7777` and can be supplied as the final argument. Enter
an action label (for example, `baseline idle` or `press accelerator slowly`)
and press Enter to record a monotonic timestamp; perform the action
immediately after the cue. Enter `/done` to stop the read-only capture.

The JSON Lines file contains `session_start`, `can_frame`, `action`, and
`session_end` records. Frame and action timestamps are milliseconds relative
to the start of the session, so observed CAN changes can later be correlated
with the user’s action cues. If the file already exists, new records are
appended as another session.
