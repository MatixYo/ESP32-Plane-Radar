/**
 * Host unit tests for core::geo — the lat/lon to pixel projection.
 *
 * This math used to sit in an anonymous namespace inside radar_display.cpp,
 * reachable only by flashing a board and looking at a 1.28" screen. It decides
 * where every aircraft is drawn, so it is worth pinning down precisely.
 */

#include <unity.h>

#include "core/geo.h"

namespace geo = core::geo;

namespace {

/** The real device view: 240x240 screen, 107 px outer ring, 10 km preset. */
geo::Viewport deviceViewport() {
  geo::Viewport vp;
  vp.center_lat = 52.3676;
  vp.center_lon = 4.9041;
  vp.center_x = 120;
  vp.center_y = 120;
  vp.outer_radius_px = 107;
  vp.outer_km = 10.0f * 4.0f / 3.0f;  // 10 km ring-3 preset
  return vp;
}

constexpr int kInset = 13;  // kAircraftInsideRingInsetPx

}  // namespace

// --- offsetKmFromCenter ------------------------------------------------------

void test_offset_is_zero_at_the_centre(void) {
  const geo::Viewport vp = deviceViewport();
  const geo::Offset o = geo::offsetKmFromCenter(vp, 52.3676f, 4.9041f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, o.dx_km);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, o.dy_km);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, o.dist_km);
}

void test_north_is_positive_dy_east_is_positive_dx(void) {
  const geo::Viewport vp = deviceViewport();

  const geo::Offset north = geo::offsetKmFromCenter(vp, 52.4576f, 4.9041f);
  TEST_ASSERT_TRUE(north.dy_km > 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, north.dx_km);
  TEST_ASSERT_FLOAT_WITHIN(0.5f, 0.09f * geo::kKmPerDeg, north.dy_km);

  const geo::Offset east = geo::offsetKmFromCenter(vp, 52.3676f, 5.0041f);
  TEST_ASSERT_TRUE(east.dx_km > 0.0f);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, east.dy_km);
}

// --- latLonToScreen ----------------------------------------------------------

void test_centre_projects_to_screen_centre(void) {
  const geo::Viewport vp = deviceViewport();
  const geo::Point p = geo::latLonToScreen(vp, 52.3676f, 4.9041f);
  TEST_ASSERT_EQUAL_INT(120, p.x);
  TEST_ASSERT_EQUAL_INT(120, p.y);
}

void test_north_moves_up_the_screen(void) {
  // Screen y grows downward, so north must decrease y. Getting this backwards
  // would mirror the entire radar and still look plausible.
  const geo::Viewport vp = deviceViewport();
  const geo::Point p = geo::latLonToScreen(vp, 52.4f, 4.9041f);
  TEST_ASSERT_EQUAL_INT(120, p.x);
  TEST_ASSERT_TRUE(p.y < 120);
}

void test_east_moves_right(void) {
  const geo::Viewport vp = deviceViewport();
  const geo::Point p = geo::latLonToScreen(vp, 52.3676f, 5.0f);
  TEST_ASSERT_TRUE(p.x > 120);
  TEST_ASSERT_EQUAL_INT(120, p.y);
}

void test_outer_km_lands_on_the_outer_ring(void) {
  const geo::Viewport vp = deviceViewport();
  const float deg = vp.outer_km / geo::kKmPerDeg;
  const geo::Point p =
      geo::latLonToScreen(vp, static_cast<float>(vp.center_lat) + deg, 4.9041f);
  TEST_ASSERT_INT_WITHIN(1, 120 - vp.outer_radius_px, p.y);
}

// --- ring containment --------------------------------------------------------

void test_inner_ring_max_km_shrinks_by_the_inset(void) {
  const geo::Viewport vp = deviceViewport();
  const float full = geo::innerRingMaxKm(vp, 0);
  const float inset = geo::innerRingMaxKm(vp, kInset);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, vp.outer_km, full);
  TEST_ASSERT_TRUE(inset < full);
}

void test_inside_outside_boundary(void) {
  const geo::Viewport vp = deviceViewport();
  const float edge = geo::innerRingMaxKm(vp, kInset);
  TEST_ASSERT_TRUE(geo::isInsideOuterRingKm(vp, edge - 0.01f, kInset));
  TEST_ASSERT_TRUE(geo::isInsideOuterRingKm(vp, edge, kInset));
  TEST_ASSERT_FALSE(geo::isInsideOuterRingKm(vp, edge + 0.01f, kInset));
}

// --- rim dots for distant traffic -------------------------------------------

void test_rim_point_declines_targets_inside_the_ring(void) {
  const geo::Viewport vp = deviceViewport();
  geo::Point out;
  TEST_ASSERT_FALSE(
      geo::rimPointForDistantTarget(vp, 52.37f, 4.905f, kInset, 118, &out));
}

void test_rim_point_declines_a_target_at_the_centre(void) {
  const geo::Viewport vp = deviceViewport();
  geo::Point out;
  TEST_ASSERT_FALSE(geo::rimPointForDistantTarget(vp, 52.3676f, 4.9041f, kInset,
                                                  118, &out));
}

void test_rim_point_bearings_are_correct(void) {
  const geo::Viewport vp = deviceViewport();
  constexpr int kRim = 118;
  geo::Point out;

  // Far north -> straight up.
  TEST_ASSERT_TRUE(
      geo::rimPointForDistantTarget(vp, 53.5f, 4.9041f, kInset, kRim, &out));
  TEST_ASSERT_INT_WITHIN(1, 120, out.x);
  TEST_ASSERT_INT_WITHIN(1, 120 - kRim, out.y);

  // Far east -> straight right.
  TEST_ASSERT_TRUE(
      geo::rimPointForDistantTarget(vp, 52.3676f, 6.5f, kInset, kRim, &out));
  TEST_ASSERT_INT_WITHIN(1, 120 + kRim, out.x);
  TEST_ASSERT_INT_WITHIN(1, 120, out.y);

  // Far south -> straight down.
  TEST_ASSERT_TRUE(
      geo::rimPointForDistantTarget(vp, 51.0f, 4.9041f, kInset, kRim, &out));
  TEST_ASSERT_INT_WITHIN(1, 120, out.x);
  TEST_ASSERT_INT_WITHIN(1, 120 + kRim, out.y);
}

void test_rim_point_is_always_on_the_rim_regardless_of_distance(void) {
  // The rim dot is a direction cue, not a distance cue: 50 km and 500 km must
  // land on the same pixel.
  const geo::Viewport vp = deviceViewport();
  constexpr int kRim = 118;
  geo::Point near_dot;
  geo::Point far_dot;
  TEST_ASSERT_TRUE(
      geo::rimPointForDistantTarget(vp, 53.0f, 4.9041f, kInset, kRim, &near_dot));
  TEST_ASSERT_TRUE(
      geo::rimPointForDistantTarget(vp, 57.0f, 4.9041f, kInset, kRim, &far_dot));
  TEST_ASSERT_EQUAL_INT(near_dot.x, far_dot.x);
  TEST_ASSERT_EQUAL_INT(near_dot.y, far_dot.y);
}

// --- speed-vector clipping ---------------------------------------------------

void test_clip_leaves_an_inside_point_untouched(void) {
  const geo::Viewport vp = deviceViewport();
  int x = 150;
  int y = 130;
  geo::clipPointToOuterRing(vp, 107, 120, 120, &x, &y);
  TEST_ASSERT_EQUAL_INT(150, x);
  TEST_ASSERT_EQUAL_INT(130, y);
}

void test_clip_pulls_an_outside_point_onto_the_ring(void) {
  const geo::Viewport vp = deviceViewport();
  int x = 400;  // way beyond the ring
  int y = 120;
  geo::clipPointToOuterRing(vp, 107, 120, 120, &x, &y);
  TEST_ASSERT_TRUE(geo::distSqFromCenter(vp, x, y) <= 107 * 107);
  TEST_ASSERT_TRUE(x > 120);  // still east of centre
  TEST_ASSERT_EQUAL_INT(120, y);
}

void test_clip_collapses_when_the_origin_is_already_outside(void) {
  // Degenerate case: nothing on the segment fits, so it collapses to the start.
  const geo::Viewport vp = deviceViewport();
  int x = 500;
  int y = 500;
  geo::clipPointToOuterRing(vp, 107, 400, 400, &x, &y);
  TEST_ASSERT_EQUAL_INT(400, x);
  TEST_ASSERT_EQUAL_INT(400, y);
}

void setUp(void) {}
void tearDown(void) {}

int main(int, char**) {
  UNITY_BEGIN();

  RUN_TEST(test_offset_is_zero_at_the_centre);
  RUN_TEST(test_north_is_positive_dy_east_is_positive_dx);

  RUN_TEST(test_centre_projects_to_screen_centre);
  RUN_TEST(test_north_moves_up_the_screen);
  RUN_TEST(test_east_moves_right);
  RUN_TEST(test_outer_km_lands_on_the_outer_ring);

  RUN_TEST(test_inner_ring_max_km_shrinks_by_the_inset);
  RUN_TEST(test_inside_outside_boundary);

  RUN_TEST(test_rim_point_declines_targets_inside_the_ring);
  RUN_TEST(test_rim_point_declines_a_target_at_the_centre);
  RUN_TEST(test_rim_point_bearings_are_correct);
  RUN_TEST(test_rim_point_is_always_on_the_rim_regardless_of_distance);

  RUN_TEST(test_clip_leaves_an_inside_point_untouched);
  RUN_TEST(test_clip_pulls_an_outside_point_onto_the_ring);
  RUN_TEST(test_clip_collapses_when_the_origin_is_already_outside);

  return UNITY_END();
}
