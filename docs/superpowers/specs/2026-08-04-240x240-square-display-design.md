# 240x240 Square Display Design

## Goal

Port the existing Plane Radar interface from a 240x240 round GC9A01 panel to a
240x240 square RGB565 SPI panel while preserving the circular radar as the
primary visual element. Use the four newly visible corners for compact status
telemetry.

This design covers the display and UI behavior. The exact square panel driver,
pin mapping, and panel offsets are selected when the target module is known.

## Current Project Assessment

- The UI already renders into a 240x240 frame sprite.
- Radar coordinates are centered at `(120, 120)`.
- The outer grid radius is 107 px, leaving a 13 px inset at the four cardinal
  directions and larger triangular regions in the four square corners.
- Aircraft, runway, range, and tag placement are all calculated against the
  circular radar, so their coordinate math does not need to change.
- The display backend is the only hardware-specific blocker: LovyanGFX is
  configured directly as `Panel_GC9A01`.
- The existing 16-bit 240x240 frame sprite uses about 115 KB and can also hold
  the corner telemetry, so the square UI does not require another framebuffer.

## Approved Layout: Floating Corner Telemetry

Keep the 214 px diameter radar centered and unframed. Do not add panels, cards,
corner wedges, or decorative brackets. Each corner gets one stable metric with
a small muted label and a colored value or icon:

| Corner | Content | Color | Source |
| --- | --- | --- | --- |
| Top-left | Wi-Fi signal bars | Green when connected, red when disconnected | `WiFi.status()` and `WiFi.RSSI()` |
| Top-right | Aircraft count | Green | `services::adsb::aircraftCount()` |
| Bottom-left | Active ring-3 range | Amber | `ui::radar::formatCurrentRing3Label()` |
| Bottom-right | Age of last successful ADS-B update | Cyan, amber when stale | Successful fetch timestamp |

Corner values use approximately 11 px cap height. Secondary labels use 7 px
cap height and remain no wider than about 40 px. Content stays 5 px from the
screen edges. Text has no filled background unless required to repair pixels
during a partial redraw.

Move the existing east-spoke range label to the bottom-left corner instead of
drawing it twice. Keep `N`, `E`, `S`, and `W` at the four edge midpoints.

## Rendering Behavior

The corner layer is drawn into the same frame sprite after the static grid and
before aircraft tags. This keeps the screen update flicker-free and gives
aircraft tags priority if an unusual label reaches a corner.

Corner state updates at the same cadence as the existing frame render. The
bottom-right age is calculated from the last successful ADS-B response, not the
last request attempt:

- `0-9 s`: cyan value, for example `4s`.
- `10-29 s`: amber value.
- `30 s` or disconnected: red `--`.

The Wi-Fi corner shows four bars for strong signal, fewer bars as RSSI drops,
and a red crossed state when Wi-Fi is unavailable. No animation is required.

## Hardware Adaptation

Add a square-panel LovyanGFX configuration alongside the existing GC9A01
configuration. A typical 240x240 ST7789 module uses `Panel_ST7789`, but the
implementation must use the actual target module's controller, panel offsets,
inversion, RGB order, SPI frequency, and pins.

Keep the round target buildable. Select the panel configuration with a board or
build flag instead of replacing `Panel_GC9A01` globally.

## Status Screens

The Wi-Fi portal and failure screens already use `config::kDisplayWidth` and
`config::kDisplayHeight`, so their central text blocks remain valid. The
connecting spinner radius of 113 px also fits the square panel. On the square
target, it may remain circular to match the radar rather than using the four
corners.

## Validation

1. Build both the existing round GC9A01 target and the new square target.
2. Confirm the panel reports a 240x240 drawable area with correct offsets,
   rotation, inversion, and RGB order.
3. Test the four range presets and both km/mi modes.
4. Test zero aircraft, maximum aircraft, runways enabled, and runways disabled.
5. Test connected, weak Wi-Fi, disconnected, fresh ADS-B data, and stale data.
6. Verify the corner text is not clipped and does not overlap `N/E/S/W` or
   aircraft tags on physical hardware.

## Prototype

- High-resolution concept: `design/prototypes/outputs/square-plane-radar-concept-a-closeup.png`
- Approved 240x240 UI reference: `design/prototypes/inputs/square-ui-option-a.png`
- Reproducible GPT-Image-2 job: `design/prototypes/packy-jobs.json`
