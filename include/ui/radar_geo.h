#pragma once

namespace ui::radar::geo {

/** Flat-earth km offset from radar center (same model as aircraft). */
void offsetKmFromCenter(float lat, float lon, float* dx_km, float* dy_km,
                        float* dist_km);

/** Pixels per km for the active range preset. */
float pxPerKm();

/** Map lat/lon to screen using current radar center and range zoom. */
void latLonToScreen(float lat, float lon, int* out_x, int* out_y);

}  // namespace ui::radar::geo
