# DeepSeek Harness Desktop

A Qt6 (QtWebEngine) desktop wrapper for the [DeepSeek Harness](https://github.com/deepseek-ai/deepseek-harness)
Web GUI. It runs the local `dsh web` backend automatically and shows the UI in a
native window — no browser needed.

## How it works

1. On launch the app probes `127.0.0.1:3080` (configurable via `--port`) and
   reuses an already-running healthy `dsh web` instance when found.
2. Otherwise it spawns the bundled runtime:
   `node <runtime>/node_modules/@deepseek-ai/dsh/lib/bin.js web --host 127.0.0.1 --port 0`
   and parses the printed `dsh web: http://127.0.0.1:<port>` line.
3. The QtWebEngine view loads that URL. Backend stdout is logged to
   `~/.local/share/deepseek/deepseek-harness-desktop/backend.log`.
4. On exit, a backend the app spawned itself is terminated gracefully.

## Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Requires Qt 6.5+ with the WebEngineWidgets component (`qt6-webengine` on Arch).

## Runtime layout

```
/usr/lib/deepseek-harness-desktop/
├── bin/deepseek-harness-desktop   # the Qt wrapper binary
└── runtime/                       # bundled dsh npm runtime (node_modules)
```

The wrapper locates the runtime next to the binary, or honors
`DSH_DESKTOP_RUNTIME_DIR` / `--runtime`, and falls back to a `dsh` on `PATH`.

## CLI options

| Option      | Meaning                                            |
|-------------|----------------------------------------------------|
| `-p, --port <port>` | Preferred port of a running backend (default 3080, `0` = always spawn fresh) |
| `--url <url>`       | Load a URL directly, skip detection/spawning       |
| `--runtime <dir>`   | Override the bundled runtime directory             |
| `--node <path>`     | Override the node executable                       |

Environment: `DSH_DESKTOP_PORT`, `DSH_DESKTOP_RUNTIME_DIR`, `DSH_DESKTOP_NODE`.

## License

MIT (this wrapper). DeepSeek Harness itself is MIT licensed; see its LICENSE.
