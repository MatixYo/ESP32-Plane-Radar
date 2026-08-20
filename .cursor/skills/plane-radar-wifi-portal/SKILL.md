---
name: plane-radar-wifi-portal
description: Configure WiFiManager captive portal, LAN config portal, mDNS, portal custom fields, and credential reset for Plane Radar. Use when adding portal settings, changing WiFi flow, or debugging setup/reconnect.
---

# Plane Radar WiFi Portal

## Portal modes

| Mode | When | Access |
|------|------|--------|
| Setup AP | No saved WiFi, or after credential wipe | SSID `PlaneRadar-Setup`, IP `192.168.4.1` |
| LAN portal | Connected to home WiFi | `http://plane-radar.local` or device IP |

Both use the same WiFiManager instance and custom fields from `core::portal`. mDNS requires `-DWM_MDNS` in `platformio.ini`.

## Custom portal fields

Defined once in `src/core/portal_params.cpp` (rendered by device WiFiManager and native HTTP server):

| Parameter ID | Label | Saved by |
|--------------|-------|----------|
| `site_1` … `site_6` | Airport N (ICAO) | `core::settings::saveSites()` |
| `use_km` | Display distances in km | `core::settings::saveKmFromPortal()` |
| `show_runways` | Show airport runways | `core::settings::saveRunwaysFromPortal()` |

All six airport slots are always visible; leave unused ones blank. Blanking all
six re-seeds `config::kDefaultSiteIdent`, so the list is never empty.

Save callback: `onPortalParamsSaved()` → `core::portal::commit()` via `wm.setSaveParamsCallback()`.

## NVS for portal state

- Namespace `wifi`, key `portal` — forces setup screen on next boot after credential wipe
- Namespace `planeradar`: `rangeIdx`, `useKm`, `showRwys`, `sites` (comma-separated ICAO), `siteIdx`

`core::settings::lat()`/`lon()` always resolve from the active airport ident via `core::airport::findAirport()`. Devices flashed before the manual coordinates were removed still hold an orphaned `radar` namespace with `lat`/`lon`; nothing reads it.

## Boot flow

1. `wifiShowsSetupScreenOnBoot()` → yellow setup screen if portal forced
2. `wifiSetupConnect()` — connect with saved creds; open captive portal only on failure
3. `wifiLoop()` — keep LAN portal alive; call every `loop()` iteration
4. `wifiReconnect()` — background reconnect; never opens captive portal

## Credential reset

| Trigger | Action |
|---------|--------|
| BOOT hold 3 s | `wifiResetCredentialsAndReboot()` |
| BOOT at power-on (documented in README) | Same wipe path |

Wipe clears: WiFi creds, the airport list — back to `config::kDefaultSiteIdent` (`core::settings::clearLocation()`) — and km/runway/terrain prefs (`core::settings::unitsReset()`). Range preset index is **not** reset.

## WiFi TX power

`WiFi.setTxPower(WIFI_POWER_8_5dBm)` in both AP start and STA connect paths. Do not increase without testing.

## Adding a new portal field

1. Add a row to `kFields[]` in `src/core/portal_params.cpp`
2. Handle `currentValue`, `htmlAttrs`, and `applyValue` for the new id
3. Persist in `commit()` or immediately in `applyValue` (checkboxes)
4. Bump `kMaxPortalFields` in `wifi_setup_device.cpp` if needed
5. Update native `appendField()` in `portal_server.cpp` if the kind is new
6. Include in credential wipe if it should reset with BOOT long-press
7. Update README WiFi portal table

## Config constants

Portal names and timing in `include/config.h`:

- `kPortalApName`, `kPortalIp`, `kPortalHostname` (`plane-radar`)
- `kWifiConnectAttemptMs`, `kWifiConnectAttempts`
- `kWifiDownGraceMs`, `kWifiReconnectIntervalMs`
- `kDoubleTapWindowMs` (500 ms), `kAdsbMinRefetchMs` (1000 ms)
