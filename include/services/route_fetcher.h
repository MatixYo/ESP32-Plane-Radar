#pragma once

namespace services::route {

// Initializes the background FreeRTOS task and queues.
void init();

// Looks up the route for a given callsign.
// Returns true and copies the route (e.g. "EDI-MUC") into 'out_route' if cached.
// Returns false if not cached, and asynchronously queues the callsign to be fetched.
bool getRoute(const char* callsign, char* out_route, int max_len);

}  // namespace services::route
