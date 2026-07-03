# Fork vs upstream contribution

Upstream: [MatixYo/ESP32-Plane-Radar](https://github.com/MatixYo/ESP32-Plane-Radar) (MIT).

## Recommended split

| Track | Purpose |
|-------|---------|
| **Upstream PR** | Generic `esp32-tft-round` port: pins, polled buttons, CH343 notes, display fixes — **no secrets**, default env stays `supermini` |
| **Your fork** | Bench branch with `include/secrets.h` (gitignored) for fixed Wi-Fi / location |

Contributing the board port upstream helps other HackerBox #0107 / 88267 owners. Keep personal network settings in `secrets.h` only on your fork.

## Prepare an upstream PR

1. Ensure `include/secrets.h` is **not** tracked (`git status` clean of secrets).
2. Default `platformio.ini` → `default_envs = supermini`.
3. Commit:
   - `include/board/esp32_tft_round.h` (pins only)
   - `include/board/supermini.h` (if shared button abstractions changed)
   - `include/config.h`, WiFi/button/display fixes
   - `docs/PORT_ESP32_TFT_ROUND.md`
   - `scripts/flash-esp32-tft.sh`, `esptool.cfg`
   - `include/secrets.h.example` (template only)
4. Open PR to `MatixYo/ESP32-Plane-Radar` with title e.g. **Add ESP32-TFT round (GC9A01 integrated) board profile**.

## Fork for bench use

```bash
gh repo fork MatixYo/ESP32-Plane-Radar --clone
cd ESP32-Plane-Radar
cp include/secrets.h.example include/secrets.h
# edit secrets.h
pio run -e esp32-tft-round -t upload --upload-port /dev/cu.wchusbserial*
```

Add your fork as `origin`, keep `upstream` pointing at MatixYo; rebase bench branches on upstream `main` when merging PR feedback.

## Secrets policy

| File | Commit? |
|------|---------|
| `include/secrets.h.example` | Yes — placeholders only |
| `include/secrets.h` | **Never** — in `.gitignore` |
| `include/board/*.h` | Yes — hardware only, no SSIDs/passwords |

If secrets were ever committed, rotate Wi-Fi password and `git filter-repo` / BFG before pushing public.
