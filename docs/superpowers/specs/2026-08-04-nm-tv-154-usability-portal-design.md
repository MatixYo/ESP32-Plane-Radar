# NM-TV-154 Usability and Portal Design

## Goal

Improve readability and configuration ergonomics for the working NM-TV-154
target without changing its validated ST7789 electrical configuration or the
existing Super Mini behavior.

## Scope

1. Enlarge the four NM-TV-154 corner telemetry values while keeping labels
   compact and preserving the 5 px safe edge.
2. Add an offline city search and preset chooser to the existing WiFiManager
   parameters page. Selecting a preset fills the existing latitude and
   longitude fields; manual coordinates remain supported.
3. Map a short T9/GPIO32 capacitive-touch press to the existing range-cycle
   action: `5`, `10`, `15`, and `25 km`.
4. Diagnose the reported WebServer unknown-handler log without replacing the
   WiFiManager portal or adding speculative routes.

## Non-Goals

- Do not change ST7789 pins, panel offset, inversion, color order, power rail,
  or backlight behavior.
- Do not replace WiFiManager with a custom web server.
- Do not perform online geocoding or add a cloud API dependency.
- Do not change the round `supermini` interaction model.

## Corner Telemetry

Keep the approved corner positions and colors. Increase the value font to the
next existing display-font size, then compute top/bottom value baselines from
the selected font height. Labels remain Font0. This retains the circular radar
and leaves the layout unframed; it only makes the primary values easier to
read. A compile-time layout helper will prove the value and label bands remain
inside the 240 px display with the 5 px insets.

## City-Assisted Coordinates

Use the same offline pattern as `deskbuddy-tv`: a browser-side search narrows a
curated list of common city presets. A selected preset sets `radar_lat` and
`radar_lon` to six-decimal values. The city name is a form convenience only;
the radar continues to persist only the validated coordinates it actually
uses. Manual latitude and longitude inputs remain below the selector.

The custom selector is an ID-free WiFiManager parameter containing small raw
HTML and browser-side JavaScript. It is inserted before the existing latitude
and longitude parameters. No new HTTP endpoint, HTTP client, NVS key, or
server type is needed.

## T9 Range Touch

On NM-TV-154 only, read the confirmed T9/GPIO32 channel with `touchRead(T9)`.
Treat a sample below the reference project threshold of 90 as touch-down.
Generate one event only after the channel returns above the threshold, and
enforce a short minimum interval to reject electrical noise. Route the event
to `onRangeTap()`, which already persists the new preset, redraws the lower
left scale, and prints the selected range.

The serial log also includes the touch raw value at each accepted event. This
provides a physical calibration observation without inventing a reset or portal
action for the touch pad.

## WebServer Diagnostic

`WebServer.cpp:648` logs before its not-found callback when no registered
URI/method handler exists. WiFiManager handles the request afterward with its
404 behavior, so the log is not evidence of a portal crash. The source does
not expose a post-registration route hook, making blind compatibility routes a
portal replacement concern rather than a minimal change.

Retain the working portal. Add project-level, rate-limited portal lifecycle
logging and keep the core log as the discriminator during physical validation.
If the captured browser/system request is one of the known probe URIs
(`/generate_204`, `/hotspot-detect.html`, or `/fwlink`) and the log is material,
the follow-up should be a deliberate WiFiManager upgrade or isolated local
patch, not an untracked package edit.

## Test and Validation Plan

- Add compile-time tests for the touch edge/debounce policy and square corner
  layout bounds.
- Add static source checks for the city selector ordering and its use of the
  existing coordinate parameter names.
- Run the new tests red before production code, then green after minimal
  implementation.
- Build both `nm-tv-154` and `supermini`.
- Upload NM-TV-154 to COM9 only after the build succeeds, then distinguish
  upload success from physical validation of touch response, corner clarity,
  city coordinate filling, and portal logs.
