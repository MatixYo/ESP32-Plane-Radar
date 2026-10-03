#pragma once

#include <cstdint>

namespace services::airport_filters {

void init();

bool showLargeAirports();
bool showMediumAirports();
bool showSmallAirports();
bool showMilitaryAirports();

bool saveFromPortal(const char* large, const char* medium,
                    const char* small, const char* military);

void clear();

bool shouldShowAirportType(uint8_t airport_type);

}  // namespace services::airport_filters