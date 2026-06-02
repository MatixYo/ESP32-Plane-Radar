# Board profiles

Plane Radar selects hardware via PlatformIO `build_flags`:

| Environment | Board header | Target |
|-------------|--------------|--------|
| `supermini` | `include/board/supermini.h` | ESP32-C3 Super Mini + wired GC9A01 (upstream default) |
| `esp32-tft-round` | `include/board/esp32_tft_round.h` | HackerBox #0107 / ESP32-TFT 88267 integrated round module |

Add `-DPLANE_RADAR_BOARD_ESP32_TFT_ROUND` only for the classic ESP32 round env.

## Local Wi-Fi / location (optional)

For development without the captive portal:

```bash
cp include/secrets.h.example include/secrets.h
# edit include/secrets.h — file is gitignored
```

Never commit `include/secrets.h`. Upstream PRs must not include credentials.

See [docs/FORK_AND_UPSTREAM.md](../docs/FORK_AND_UPSTREAM.md).
