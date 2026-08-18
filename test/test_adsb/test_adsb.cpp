/**
 * Host unit tests for core::adsb's response decoder.
 *
 * The decoder walks the reply one aircraft at a time instead of buffering it,
 * because the whole body no longer fits in a single device allocation. That
 * makes the framing — finding the "ac" array, telling one element from the
 * next, stopping at the end — hand-rolled, so it is pinned down here.
 */

#include <unity.h>

#include <string>

#include "core/adsb.h"

namespace adsb = core::adsb;

namespace {

/** Two aircraft, one of them on the ground, with "ac" after another key. */
const char* twoAircraft() {
  return R"({"now":1712345678,"msg":"No error","ac":[
    {"hex":"3c6444","flight":"DLH8AB  ","t":"A320","lat":47.12,"lon":15.51,
     "alt_baro":34000,"track":91.5,"true_heading":93.0,"gs":420.5},
    {"hex":"4401f2","flight":"AUA123  ","t":"B738","lat":47.05,"lon":15.39,
     "alt_baro":"ground","track":0,"gs":0}
  ],"total":2})";
}

std::string manyAircraft(size_t count) {
  std::string json = R"({"ac":[)";
  for (size_t i = 0; i < count; ++i) {
    if (i > 0) {
      json += ',';
    }
    json += R"({"hex":"abc","lat":47.1,"lon":15.4,"alt_baro":10000,"gs":300})";
  }
  json += R"(],"total":)";
  json += std::to_string(count);
  json += '}';
  return json;
}

}  // namespace

// --- Field decoding ----------------------------------------------------------

void test_decodes_an_airborne_aircraft(void) {
  TEST_ASSERT_TRUE(adsb::parseResponse(twoAircraft()));
  TEST_ASSERT_EQUAL_size_t(1, adsb::aircraftCount());

  const adsb::Aircraft& ac = adsb::aircraftList()[0];
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 47.12f, ac.lat);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 15.51f, ac.lon);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 93.0f, ac.nose_deg);   // true_heading wins
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 91.5f, ac.track_deg);  // track wins
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 420.5f, ac.gs_knots);
  TEST_ASSERT_EQUAL_STRING("DLH8AB", ac.callsign);  // trailing pad trimmed
  TEST_ASSERT_EQUAL_STRING("A320", ac.type);
  TEST_ASSERT_EQUAL_STRING("34000 ft", ac.alt);
}

void test_ground_aircraft_are_skipped(void) {
  TEST_ASSERT_TRUE(adsb::parseResponse(twoAircraft()));
  // The ground target is the second element, so skipping it also proves the
  // scanner keeps walking past an element it discards.
  TEST_ASSERT_EQUAL_size_t(1, adsb::aircraftCount());
  TEST_ASSERT_EQUAL_STRING("DLH8AB", adsb::aircraftList()[0].callsign);
}

void test_aircraft_without_a_position_is_skipped(void) {
  TEST_ASSERT_TRUE(adsb::parseResponse(
      R"({"ac":[{"hex":"abc","alt_baro":9000},)"
      R"({"hex":"def","lat":47.0,"lon":15.0}]})"));
  TEST_ASSERT_EQUAL_size_t(1, adsb::aircraftCount());
  TEST_ASSERT_EQUAL_STRING("def", adsb::aircraftList()[0].callsign);
}

// --- Framing -----------------------------------------------------------------

void test_quiet_sky_reports_no_aircraft(void) {
  TEST_ASSERT_TRUE(adsb::parseResponse(R"({"ac":null,"total":0})"));
  TEST_ASSERT_EQUAL_size_t(0, adsb::aircraftCount());
}

void test_empty_array_reports_no_aircraft(void) {
  TEST_ASSERT_TRUE(adsb::parseResponse(R"({"ac":[],"total":0})"));
  TEST_ASSERT_EQUAL_size_t(0, adsb::aircraftCount());
}

void test_missing_array_is_an_error(void) {
  TEST_ASSERT_FALSE(adsb::parseResponse(R"({"msg":"No data","total":0})"));
  TEST_ASSERT_EQUAL_size_t(0, adsb::aircraftCount());
}

void test_truncated_body_is_an_error(void) {
  const std::string full = manyAircraft(4);
  const std::string cut = full.substr(0, full.size() / 2);
  TEST_ASSERT_FALSE(adsb::parseResponse(cut.c_str()));
}

void test_count_is_capped_at_the_store_size(void) {
  const std::string json = manyAircraft(adsb::kMaxAircraft + 20);
  TEST_ASSERT_TRUE(adsb::parseResponse(json.c_str()));
  TEST_ASSERT_EQUAL_size_t(adsb::kMaxAircraft, adsb::aircraftCount());
}

void test_a_short_reply_after_a_long_one_replaces_the_store(void) {
  const std::string many = manyAircraft(10);
  TEST_ASSERT_TRUE(adsb::parseResponse(many.c_str()));
  TEST_ASSERT_EQUAL_size_t(10, adsb::aircraftCount());

  TEST_ASSERT_TRUE(adsb::parseResponse(R"({"ac":[],"total":0})"));
  TEST_ASSERT_EQUAL_size_t(0, adsb::aircraftCount());
}

// --- URL ---------------------------------------------------------------------

void test_url_carries_the_radius_in_nautical_miles(void) {
  char url[160];
  adsb::buildUrl(url, sizeof(url), 47.0753, 15.4062, 108.9276f);
  TEST_ASSERT_EQUAL_STRING(
      "https://opendata.adsb.fi/api/v3/lat/47.075300/lon/15.406200/dist/58.8",
      url);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_decodes_an_airborne_aircraft);
  RUN_TEST(test_ground_aircraft_are_skipped);
  RUN_TEST(test_aircraft_without_a_position_is_skipped);

  RUN_TEST(test_quiet_sky_reports_no_aircraft);
  RUN_TEST(test_empty_array_reports_no_aircraft);
  RUN_TEST(test_missing_array_is_an_error);
  RUN_TEST(test_truncated_body_is_an_error);
  RUN_TEST(test_count_is_capped_at_the_store_size);
  RUN_TEST(test_a_short_reply_after_a_long_one_replaces_the_store);

  RUN_TEST(test_url_carries_the_radius_in_nautical_miles);

  return UNITY_END();
}
