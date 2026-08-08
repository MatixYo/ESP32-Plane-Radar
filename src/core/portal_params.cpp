#include "core/portal_params.h"

#include <cstdio>
#include <cstring>

#include "core/platform.h"
#include "core/settings.h"

namespace core::portal {

namespace {

constexpr int kCoordLen = 20;
constexpr char kCoordAttrs[] = " type=\"number\" step=\"0.000001\"";

constexpr Field kFields[] = {
    {"radar_lat", "Latitude (deg)", kCoordAttrs, Kind::kNumber, kCoordLen,
     false},
    {"radar_lon", "Longitude (deg)", kCoordAttrs, Kind::kNumber, kCoordLen,
     false},
    {"use_km", "Display distances in km", "type=\"checkbox\"",
     Kind::kCheckbox, 2, true},
    {"show_runways", "Show airport runways", "type=\"checkbox\"",
     Kind::kCheckbox, 2, true},
};

/**
 * Latitude and longitude are staged rather than applied immediately: they are
 * validated as a pair, and a form that submits a good latitude with a bad
 * longitude must change neither.
 */
char s_pending_lat[kCoordLen + 1] = "";
char s_pending_lon[kCoordLen + 1] = "";

bool isField(const Field& f, const char* id) { return strcmp(f.id, id) == 0; }

}  // namespace

const Field* fields() { return kFields; }

size_t fieldCount() { return sizeof(kFields) / sizeof(kFields[0]); }

void currentValue(const Field& field, char* buf, size_t len) {
  if (len == 0) {
    return;
  }
  if (field.kind == Kind::kCheckbox) {
    // State lives in the `checked` attribute, not the value; see the header.
    snprintf(buf, len, "T");
    return;
  }
  if (isField(field, "radar_lat")) {
    snprintf(buf, len, "%.6f", settings::lat());
  } else if (isField(field, "radar_lon")) {
    snprintf(buf, len, "%.6f", settings::lon());
  } else {
    buf[0] = '\0';
  }
}

void htmlAttrs(const Field& field, char* buf, size_t len) {
  if (len == 0) {
    return;
  }
  if (field.kind != Kind::kCheckbox) {
    snprintf(buf, len, "%s", field.html_attrs);
    return;
  }

  bool on = false;
  if (isField(field, "use_km")) {
    on = settings::useKm();
  } else if (isField(field, "show_runways")) {
    on = settings::showRunways();
  }

  snprintf(buf, len, "%s%s", field.html_attrs, on ? " checked" : "");
}

void applyValue(const Field& field, const char* value) {
  if (isField(field, "radar_lat")) {
    strncpy(s_pending_lat, value != nullptr ? value : "",
            sizeof(s_pending_lat) - 1);
    s_pending_lat[sizeof(s_pending_lat) - 1] = '\0';
  } else if (isField(field, "radar_lon")) {
    strncpy(s_pending_lon, value != nullptr ? value : "",
            sizeof(s_pending_lon) - 1);
    s_pending_lon[sizeof(s_pending_lon) - 1] = '\0';
  } else if (isField(field, "use_km")) {
    settings::saveKmFromPortal(value);
  } else if (isField(field, "show_runways")) {
    settings::saveRunwaysFromPortal(value);
  }
}

bool applyValueById(const char* id, const char* value) {
  if (id == nullptr) {
    return false;
  }
  for (size_t i = 0; i < fieldCount(); ++i) {
    if (isField(kFields[i], id)) {
      applyValue(kFields[i], value);
      return true;
    }
  }
  return false;
}

void commit() {
  if (!settings::saveLocationFromStrings(s_pending_lat, s_pending_lon)) {
    platform::logf(
        "Invalid lat/lon in portal — keeping previous location\n");
  }
  s_pending_lat[0] = '\0';
  s_pending_lon[0] = '\0';
}

}  // namespace core::portal
