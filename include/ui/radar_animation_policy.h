#pragma once

#include <cstddef>

namespace ui::radar {

constexpr unsigned long kSweepPeriodMs = 12000UL;
constexpr unsigned long kSuperMiniAnimationIntervalMs = 100UL;
constexpr unsigned long kNmTv154AnimationIntervalMs = 50UL;

constexpr unsigned long animationIntervalMs(bool is_nm_tv_154) {
  return is_nm_tv_154 ? kNmTv154AnimationIntervalMs
                       : kSuperMiniAnimationIntervalMs;
}

constexpr unsigned long sweepPhaseMs(unsigned long now_ms) {
  return now_ms % kSweepPeriodMs;
}

constexpr bool animationNeeded(bool sweep_enabled, size_t aircraft_count) {
  return sweep_enabled || aircraft_count > 0;
}

}  // namespace ui::radar
