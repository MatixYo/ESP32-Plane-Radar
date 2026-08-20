#include "core/portal_params.h"

#include <cstdio>
#include <cstring>

#include "core/settings.h"

namespace core::portal {

namespace {

constexpr char kSiteTextAttrs[] =
    " type=\"text\" maxlength=\"4\" autocapitalize=\"characters\"";

constexpr Field kFields[] = {
    {"site_1", "Airport 1 (ICAO)", kSiteTextAttrs, Kind::kText, 5, false},
    {"site_2", "Airport 2 (ICAO)", kSiteTextAttrs, Kind::kText, 5, false},
    {"site_3", "Airport 3 (ICAO)", kSiteTextAttrs, Kind::kText, 5, false},
    {"site_4", "Airport 4 (ICAO)", kSiteTextAttrs, Kind::kText, 5, false},
    {"site_5", "Airport 5 (ICAO)", kSiteTextAttrs, Kind::kText, 5, false},
    {"site_6", "Airport 6 (ICAO)", kSiteTextAttrs, Kind::kText, 5, false},
    {"use_km", "Display distances in km", "type=\"checkbox\"",
     Kind::kCheckbox, 2, true},
    {"show_runways", "Show airport runways", "type=\"checkbox\"",
     Kind::kCheckbox, 2, true},
    {"show_terrain", "Show terrain", "type=\"checkbox\"", Kind::kCheckbox, 2,
     true},
};

char s_pending_sites[settings::kMaxSites][6] = {};

bool isField(const Field& f, const char* id) {
  return f.id != nullptr && id != nullptr && strcmp(f.id, id) == 0;
}

bool isSiteField(const Field& f) {
  return f.id != nullptr && strncmp(f.id, "site_", 5) == 0 &&
         f.id[5] >= '1' && f.id[5] <= '6' && f.id[6] == '\0';
}

size_t siteFieldSlot(const Field& f) {
  return static_cast<size_t>(f.id[5] - '1');
}

}  // namespace

const Field* fields() { return kFields; }

size_t fieldCount() { return sizeof(kFields) / sizeof(kFields[0]); }

void currentValue(const Field& field, char* buf, size_t len) {
  if (len == 0) {
    return;
  }
  if (field.kind == Kind::kCheckbox) {
    snprintf(buf, len, "T");
    return;
  }
  if (isSiteField(field)) {
    snprintf(buf, len, "%s", settings::siteSlotIdent(siteFieldSlot(field)));
    return;
  }
  buf[0] = '\0';
}

void htmlAttrs(const Field& field, char* buf, size_t len) {
  if (len == 0) {
    return;
  }
  if (field.kind == Kind::kCheckbox) {
    bool on = false;
    if (isField(field, "use_km")) {
      on = settings::useKm();
    } else if (isField(field, "show_runways")) {
      on = settings::showRunways();
    } else if (isField(field, "show_terrain")) {
      on = settings::showTerrain();
    }
    snprintf(buf, len, "%s%s", field.html_attrs, on ? " checked" : "");
    return;
  }

  snprintf(buf, len, "%s", field.html_attrs);
}

void applyValue(const Field& field, const char* value) {
  if (isSiteField(field)) {
    const size_t slot = siteFieldSlot(field);
    strncpy(s_pending_sites[slot], value != nullptr ? value : "",
            sizeof(s_pending_sites[slot]) - 1);
    s_pending_sites[slot][sizeof(s_pending_sites[slot]) - 1] = '\0';
    return;
  }
  if (isField(field, "use_km")) {
    settings::saveKmFromPortal(value);
  } else if (isField(field, "show_runways")) {
    settings::saveRunwaysFromPortal(value);
  } else if (isField(field, "show_terrain")) {
    settings::saveTerrainFromPortal(value);
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
  const char* idents[settings::kMaxSites];
  size_t n = 0;
  for (size_t i = 0; i < settings::kMaxSites; ++i) {
    if (s_pending_sites[i][0] != '\0') {
      idents[n++] = s_pending_sites[i];
    }
  }
  settings::saveSites(idents, n);

  for (size_t i = 0; i < settings::kMaxSites; ++i) {
    s_pending_sites[i][0] = '\0';
  }
}

}  // namespace core::portal
