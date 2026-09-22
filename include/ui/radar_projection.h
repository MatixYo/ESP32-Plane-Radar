#pragma once

namespace ui::radar {

constexpr float kKmPerDeg = 111.0f;
constexpr float kDegToRad = 3.14159265f / 180.0f;

/** Offset of lat/lon from the radar center, in km. */
void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km);

/** Flat lat/lon as x/y: 1 deg ~ 111 km, north = screen up. */
void latLonToScreen(float lat, float lon, int* out_x, int* out_y);

int distSqFromCenter(int x, int y);

/** Pull (x1,y1) back along the segment from (x0,y0) until inside the ring. */
void clipPointToOuterRing(int x0, int y0, int* x1, int* y1);

}  // namespace ui::radar
