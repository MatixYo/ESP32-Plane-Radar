#pragma once

#include "services/adsb_client.h"

namespace ui {

void aircraftDetailInit();
void aircraftDetailDraw(const services::adsb::Aircraft& ac);

}  // namespace ui
