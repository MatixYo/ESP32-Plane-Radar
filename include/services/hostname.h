#pragma once

namespace services::hostname {

/** Load saved hostname from NVS, or use config default. Call once before WiFi setup. */
void init();

/** Bare mDNS label (no ".local"), for MDNS.begin()/setHostname(). */
const char* value();

/** "<value>.local", for on-screen/log display. */
const char* hostUrl();

/** Validate, persist to NVS, and update runtime values. False (unchanged) if invalid. */
bool saveFromString(const char* hostname);

/** Reset to the compiled default (e.g. with WiFi credential reset). */
void clear();

}  // namespace services::hostname
