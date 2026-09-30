#pragma once

namespace services::mqtt {

/** Load config, build the node id. Call once in setup() after location init. */
void init();

/** Non-blocking: connect, publish discovery, poll, publish telemetry.
 *  Call every loop() iteration; never from the ADS-B fetch task. */
void loop();

}  // namespace services::mqtt
