#pragma once

#include "hardware/lgfx_config.hpp"

extern LGFX tft;

void displayInit();
/** Turn panel backlight on after init/first draw (keeps boot garbage hidden). */
void displayBacklightOn();
