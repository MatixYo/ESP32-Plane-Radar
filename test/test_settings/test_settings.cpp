/**
 * Host unit tests for the pure logic in core::settings.
 *
 * These lock the behaviours that the device/native split is most likely to
 * silently change: coordinate parsing, the WiFiManager checkbox quirk, and the
 * ring-3 label rounding that the radar's scale label is drawn from.
 */

#include <unity.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "core/settings.h"
#include "core/airport_find.h"

namespace cs = core::settings;
namespace ca = core::airport;

// --- parseCoord --------------------------------------------------------------

void test_parseCoord_accepts_plain_and_signed(void) {
  double v = 0.0;
  TEST_ASSERT_TRUE(cs::parseCoord("52.3676", &v));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 52.3676, v);

  TEST_ASSERT_TRUE(cs::parseCoord("-4.9041", &v));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, -4.9041, v);

  TEST_ASSERT_TRUE(cs::parseCoord("0", &v));
  TEST_ASSERT_DOUBLE_WITHIN(1e-9, 0.0, v);
}

void test_parseCoord_rejects_empty_and_null(void) {
  double v = 0.0;
  TEST_ASSERT_FALSE(cs::parseCoord(nullptr, &v));
  TEST_ASSERT_FALSE(cs::parseCoord("", &v));
}

void test_parseCoord_rejects_trailing_garbage(void) {
  // The strictness matters: the portal is free text, and "52.3676 N" silently
  // parsing as 52.3676 would hide a user error rather than reporting it.
  double v = 0.0;
  TEST_ASSERT_FALSE(cs::parseCoord("52.3676 N", &v));
  TEST_ASSERT_FALSE(cs::parseCoord("52,3676", &v));
  TEST_ASSERT_FALSE(cs::parseCoord("abc", &v));
  TEST_ASSERT_FALSE(cs::parseCoord("12abc", &v));
}

// --- validLatLon -------------------------------------------------------------

void test_validLatLon_accepts_range_including_bounds(void) {
  TEST_ASSERT_TRUE(cs::validLatLon(0.0, 0.0));
  TEST_ASSERT_TRUE(cs::validLatLon(52.3676, 4.9041));
  TEST_ASSERT_TRUE(cs::validLatLon(90.0, 180.0));
  TEST_ASSERT_TRUE(cs::validLatLon(-90.0, -180.0));
}

void test_validLatLon_rejects_out_of_range(void) {
  TEST_ASSERT_FALSE(cs::validLatLon(90.1, 0.0));
  TEST_ASSERT_FALSE(cs::validLatLon(-90.1, 0.0));
  TEST_ASSERT_FALSE(cs::validLatLon(0.0, 180.1));
  TEST_ASSERT_FALSE(cs::validLatLon(0.0, -180.1));
}

// --- portalCheckboxChecked ---------------------------------------------------

void test_checkbox_single_TF_means_submitted(void) {
  // WiFiManager submits the value= attribute, which the portal prefills with
  // "T" regardless of state; the browser only sends the field at all when the
  // box is ticked. So a bare T or F both mean "checked".
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("T"));
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("t"));
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("F"));
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("f"));
}

void test_checkbox_accepts_conventional_on(void) {
  TEST_ASSERT_TRUE(cs::portalCheckboxChecked("on"));
}

void test_checkbox_rejects_absent_and_unknown(void) {
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked(nullptr));
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked(""));
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked("TT"));
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked("off"));
  TEST_ASSERT_FALSE(cs::portalCheckboxChecked("true"));
}

// --- formatRing3Label --------------------------------------------------------

void test_format_nm_is_the_default_and_is_exact(void) {
  // The presets are authored in NM, so the default labels must come back as the
  // exact round numbers they were defined with — no rounding drift through the
  // km round trip.
  char buf[12];
  const char* expected[] = {"10NM", "20NM", "40NM", "80NM"};
  for (size_t i = 0; i < cs::kRangePresetCount; ++i) {
    cs::formatRing3Label(buf, sizeof(buf), cs::kRangePresets[i].ring3_km,
                         /*use_km=*/false);
    TEST_ASSERT_EQUAL_STRING(expected[i], buf);
  }
}

void test_format_km_when_toggled(void) {
  // Matches the table in README.md: 10/20/40/80 NM -> 19/37/74/148 km.
  char buf[12];
  const char* expected[] = {"19km", "37km", "74km", "148km"};
  for (size_t i = 0; i < cs::kRangePresetCount; ++i) {
    cs::formatRing3Label(buf, sizeof(buf), cs::kRangePresets[i].ring3_km,
                         /*use_km=*/true);
    TEST_ASSERT_EQUAL_STRING(expected[i], buf);
  }
}

// --- range presets -----------------------------------------------------------

void test_outer_km_is_ring3_over_three_quarters(void) {
  for (size_t i = 0; i < cs::kRangePresetCount; ++i) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, cs::kRangePresets[i].ring3_km * 4.0f / 3.0f,
                             cs::kRangePresets[i].outer_km);
  }
}

void test_rangeNext_cycles_and_wraps(void) {
  const uint8_t start = cs::rangeIndex();
  for (size_t i = 0; i < cs::kRangePresetCount; ++i) {
    cs::rangeNext();
  }
  TEST_ASSERT_EQUAL_UINT8(start, cs::rangeIndex());

  cs::rangeNext();
  TEST_ASSERT_EQUAL_UINT8((start + 1) % cs::kRangePresetCount, cs::rangeIndex());
}

// --- unitsReset asymmetry ----------------------------------------------------

void test_unitsReset_leaves_range_alone(void) {
  // Deliberate asymmetry: a Wi-Fi wipe resets units and the runway overlay but
  // keeps the user's chosen zoom.
  cs::saveKmFromPortal("T");
  cs::saveRunwaysFromPortal(nullptr);
  cs::rangeNext();
  const uint8_t range_before = cs::rangeIndex();

  cs::unitsReset();

  TEST_ASSERT_FALSE(cs::useKm());
  TEST_ASSERT_TRUE(cs::showRunways());
  TEST_ASSERT_EQUAL_UINT8(range_before, cs::rangeIndex());
}

// --- airport lookup ----------------------------------------------------------

void test_findAirport_resolves_known_large_airport(void) {
  data::large_airports::Airport ap{};
  TEST_ASSERT_TRUE(ca::findAirport("LOWG", &ap));
  TEST_ASSERT_EQUAL_STRING("LOWG", ap.ident);
}

void test_findAirport_rejects_unknown(void) {
  TEST_ASSERT_FALSE(ca::findAirport("ZZZZ", nullptr));
  TEST_ASSERT_FALSE(ca::findAirport("LOW", nullptr));
}

// --- site list ---------------------------------------------------------------

void test_saveSites_resolves_and_cycles(void) {
  cs::clearLocation();
  const char* idents[] = {"LOWG", "LOWW"};
  TEST_ASSERT_TRUE(cs::saveSites(idents, 2));
  TEST_ASSERT_EQUAL_size_t(2, cs::siteCount());
  TEST_ASSERT_EQUAL_STRING("LOWG", cs::siteActiveIdent());

  data::large_airports::Airport ap{};
  TEST_ASSERT_TRUE(ca::findAirport("LOWG", &ap));
  double lat0 = 0.0;
  double lon0 = 0.0;
  lat0 = static_cast<double>(ap.lat_e7) / 1.0e7;
  lon0 = static_cast<double>(ap.lon_e7) / 1.0e7;
  TEST_ASSERT_DOUBLE_WITHIN(1e-5, lat0, cs::lat());
  TEST_ASSERT_DOUBLE_WITHIN(1e-5, lon0, cs::lon());

  cs::siteNext();
  TEST_ASSERT_EQUAL_STRING("LOWW", cs::siteActiveIdent());
  TEST_ASSERT_EQUAL_UINT8(1, cs::siteIndex());

  cs::siteNext();
  TEST_ASSERT_EQUAL_STRING("LOWG", cs::siteActiveIdent());
}

void test_saveSites_rejects_unknown_codes(void) {
  cs::clearLocation();
  const char* idents[] = {"LOWG", "ZZZZ", "LOWW"};
  cs::saveSites(idents, 3);
  TEST_ASSERT_EQUAL_size_t(2, cs::siteCount());
  TEST_ASSERT_EQUAL_STRING("LOWG", cs::siteIdent(0));
  TEST_ASSERT_EQUAL_STRING("LOWW", cs::siteIdent(1));
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  // Redirect the settings store to a scratch file before anything touches it,
  // so a test run never reads or clobbers the developer's real
  // ~/.plane-radar/settings.json. The path is resolved lazily on first use, so
  // this must happen before the first core::settings call.
  static const char* const kScratch = "/tmp/plane-radar-test-settings.json";
  setenv("PLANE_RADAR_SETTINGS", kScratch, 1);
  std::remove(kScratch);

  UNITY_BEGIN();

  RUN_TEST(test_parseCoord_accepts_plain_and_signed);
  RUN_TEST(test_parseCoord_rejects_empty_and_null);
  RUN_TEST(test_parseCoord_rejects_trailing_garbage);

  RUN_TEST(test_validLatLon_accepts_range_including_bounds);
  RUN_TEST(test_validLatLon_rejects_out_of_range);

  RUN_TEST(test_checkbox_single_TF_means_submitted);
  RUN_TEST(test_checkbox_accepts_conventional_on);
  RUN_TEST(test_checkbox_rejects_absent_and_unknown);

  RUN_TEST(test_format_nm_is_the_default_and_is_exact);
  RUN_TEST(test_format_km_when_toggled);

  RUN_TEST(test_outer_km_is_ring3_over_three_quarters);
  RUN_TEST(test_rangeNext_cycles_and_wraps);
  RUN_TEST(test_unitsReset_leaves_range_alone);

  RUN_TEST(test_findAirport_resolves_known_large_airport);
  RUN_TEST(test_findAirport_rejects_unknown);
  RUN_TEST(test_saveSites_resolves_and_cycles);
  RUN_TEST(test_saveSites_rejects_unknown_codes);

  return UNITY_END();
}
