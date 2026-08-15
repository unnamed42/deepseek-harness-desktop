# DeepSeek Harness Desktop

A Qt6 (QtWebEngine) desktop wrapper for the [DeepSeek Harness](https://github.com/deepseek-ai/deepseek-harness)
Web GUI. It runs the local `dsh web` backend automatically and shows the UI in a
native window — no browser needed.

## Architecture

The wrapper does **not** bundle the harness. It requires the `dsh` command on
`PATH` — on Arch, install the AUR package `deepseek-harness-bin` (a dependency
of the `deepseek-harness-bin-desktop` package).

Backend selection order on startup:

1. **Probe** `127.0.0.1:<port>` (default 3080, `--port` to change). A healthy
   DeepSeek Harness instance there (verified via the `__DSH_BOOT__` boot
   manifest) is reused directly.
2. **systemd user service** — "if an equivalent service already exists, don't
   add another one":
   - a registered `dsh-web.service` is only started (never re-added);
   - any *other* user unit whose `ExecStart` runs `dsh web` counts as
     equivalent and is used instead;
   - only when nothing equivalent exists does the wrapper
     `systemctl --user enable --now dsh-web.service` (the unit shipped at
     `/usr/lib/systemd/user/dsh-web.service`, port configurable via
     `Environment=DSH_WEB_PORT`); the real service port is read back from the
     unit's `ExecStart`/`Environment`.
3. **Fallback** — if systemd is unavailable or `--no-service` is given, the
   wrapper spawns `dsh web --host 127.0.0.1 --port 0` itself and parses the
   printed `dsh web: http://127.0.0.1:<port>` line. Such a self-spawned
   backend is terminated on exit; systemd-managed backends are left running.

Backend stdout is logged to
`~/.local/share/deepseek/deepseek-harness-desktop/backend.log`
(set `DSH_DESKTOP_DEBUG=1` to also log the wrapper's status steps there).

## Build

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Requires Qt 6.5+ with the WebEngineWidgets component (`qt6-webengine` on Arch).

## Install layout

```
/usr/lib/deepseek-harness-desktop/bin/deepseek-harness-desktop   # wrapper binary
/usr/bin/deepseek-harness-desktop -> ../lib/...                  # symlink
/usr/lib/systemd/user/dsh-web.service                            # backend user service
/usr/share/applications/deepseek-harness-desktop.desktop
/usr/share/icons/hicolor/.../apps/deepseek-harness-desktop.{png,svg}
```

## CLI options

| Option            | Meaning                                                        |
|-------------------|----------------------------------------------------------------|
| `-p, --port <p>`  | Preferred port of a running backend (default 3080, `0` = always spawn fresh) |
| `--url <url>`     | Load a URL directly, skip detection/service management         |
| `--no-service`    | Never touch systemd user services; always run `dsh web` directly |

Environment: `DSH_DESKTOP_PORT`, `DSH_DESKTOP_NO_SERVICE`, `DSH_DESKTOP_DEBUG`.

## License

MIT (this wrapper). DeepSeek Harness itself is MIT licensed; see its LICENSE.
