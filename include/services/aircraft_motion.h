#pragma once

#include <cmath>

#include "services/adsb_client.h"

namespace services::adsb::motion {

constexpr unsigned long kBlendDurationMs = 1200UL;
constexpr unsigned long kMaxPredictionMs = 10000UL;
constexpr float kKnotsToMetersPerSecond = 0.514444f;
constexpr float kMetersPerDegreeLatitude = 111320.0f;

struct Position {
  float lat;
  float lon;
};

struct MotionVector {
  float north;
  float east;
};

struct AircraftTrack {
  Aircraft aircraft;
  Position blend_from;
  unsigned long updated_ms;
};

constexpr unsigned long elapsedMs(unsigned long now_ms,
                                  unsigned long then_ms) {
  return now_ms - then_ms;
}

constexpr unsigned long predictionElapsedMs(unsigned long elapsed_ms) {
  return elapsed_ms < kMaxPredictionMs ? elapsed_ms : kMaxPredictionMs;
}

constexpr float predictedDistanceMeters(float ground_speed_knots,
                                        unsigned long elapsed_ms) {
  return ground_speed_knots * kKnotsToMetersPerSecond *
         (static_cast<float>(predictionElapsedMs(elapsed_ms)) / 1000.0f);
}

constexpr float normalizeDegrees(float degrees) {
  while (degrees < 0.0f) {
    degrees += 360.0f;
  }
  while (degrees >= 360.0f) {
    degrees -= 360.0f;
  }
  return degrees;
}

constexpr MotionVector trackUnitVector(float track_deg) {
  const float normalized = normalizeDegrees(track_deg);
  if (normalized == 0.0f) {
    return {1.0f, 0.0f};
  }
  if (normalized == 90.0f) {
    return {0.0f, 1.0f};
  }
  if (normalized == 180.0f) {
    return {-1.0f, 0.0f};
  }
  if (normalized == 270.0f) {
    return {0.0f, -1.0f};
  }
  const float heading_rad = normalized * 0.01745329252f;
  return {cosf(heading_rad), sinf(heading_rad)};
}

constexpr float clamp01(float value) {
  return value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
}

constexpr float smoothstep(float value) {
  const float t = clamp01(value);
  return t * t * (3.0f - 2.0f * t);
}

constexpr bool hasStableId(const Aircraft& aircraft) {
  return aircraft.id[0] != '\0';
}

constexpr Position predictPosition(const Aircraft& aircraft,
                                   unsigned long elapsed_ms) {
  if (aircraft.gs_knots <= 0.0f) {
    return {aircraft.lat, aircraft.lon};
  }

  const float distance_m = predictedDistanceMeters(aircraft.gs_knots, elapsed_ms);
  const MotionVector direction = trackUnitVector(aircraft.track_deg);
  const float north_m = distance_m * direction.north;
  const float east_m = distance_m * direction.east;
  const float latitude_rad = aircraft.lat * 0.01745329252f;
  const float lon_scale = kMetersPerDegreeLatitude * cosf(latitude_rad);

  return {
      aircraft.lat + north_m / kMetersPerDegreeLatitude,
      fabsf(lon_scale) < 1.0f ? aircraft.lon
                              : aircraft.lon + east_m / lon_scale,
  };
}

constexpr Position displayPosition(const AircraftTrack& track,
                                   unsigned long now_ms) {
  const unsigned long elapsed = elapsedMs(now_ms, track.updated_ms);
  const Position target = predictPosition(track.aircraft, elapsed);
  const float alpha = smoothstep(static_cast<float>(elapsed) /
                                 static_cast<float>(kBlendDurationMs));
  return {
      track.blend_from.lat + alpha * (target.lat - track.blend_from.lat),
      track.blend_from.lon + alpha * (target.lon - track.blend_from.lon),
  };
}

constexpr AircraftTrack makeInitialTrack(const Aircraft& aircraft,
                                         unsigned long now_ms) {
  return {aircraft, {aircraft.lat, aircraft.lon}, now_ms};
}

constexpr AircraftTrack updateTrack(const AircraftTrack& previous,
                                    const Aircraft& aircraft,
                                    unsigned long now_ms) {
  return {aircraft, displayPosition(previous, now_ms), now_ms};
}

}  // namespace services::adsb::motion
