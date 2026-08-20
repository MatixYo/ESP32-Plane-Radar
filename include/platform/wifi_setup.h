#pragma once

/**
 * Station association state, as main.cpp's loop uses it.
 *
 * Deliberately equivalent to WiFi.status() == WL_CONNECTED and nothing more.
 * The stricter internal wifiLinkUp() additionally requires a non-zero local IP;
 * substituting it here would change the WiFi-lost / grace / reconnect timing in
 * loop(), so the two must stay distinct.
 */
bool wifiIsConnected();

/** True when the next boot should show the setup screen first (after credential reset). */
bool wifiShowsSetupScreenOnBoot();
void wifiResetCredentialsAndReboot();
/** Boot flow: connect with UI, open portal only if saved creds fail. */
bool wifiSetupConnect();
/** Reconnect using saved creds; never opens the captive portal. */
bool wifiReconnect();
/** Keeps the LAN config portal alive; call every loop() iteration. */
void wifiLoop();
bool wifiBootButtonPressed();
/** GPIO + interrupt setup; call once early in setup(). */
void bootButtonInit();
/**
 * Pop the oldest latched tap; *tap_ms is the millis() of its release edge.
 *
 * Queued rather than flagged, and timestamped at the edge rather than here, so
 * a gesture made while a blocking HTTP request holds the loop is classified by
 * when it happened instead of by when it was finally consumed.
 */
bool bootButtonConsumeTap(unsigned long* tap_ms);
/** Call each loop iteration; triggers WiFi reset on long hold. */
void bootButtonPollLongPress();
