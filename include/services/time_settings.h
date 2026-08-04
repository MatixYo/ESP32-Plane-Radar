#pragma once

namespace services::time_settings {

/** Load persisted clock display settings; call once during boot. */
void init();
/** Active POSIX timezone string, either the default or a saved manual value. */
const char* timeZone();
/** True when the portal's manual timezone option is enabled. */
bool usesManualTimeZone();
/** True for HH:MM; false for 12-hour time with AM/PM. */
bool uses24HourClock();
/** Persist portal values and apply the selected timezone immediately. */
void saveFromPortal(const char* manual_timezone_value, const char* timezone_value,
                    const char* clock_24h_value);

}  // namespace services::time_settings