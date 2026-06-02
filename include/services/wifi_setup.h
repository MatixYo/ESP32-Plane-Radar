#pragma once

/** True when the next boot should show the setup screen first (after credential reset). */
bool wifiShowsSetupScreenOnBoot();
void wifiResetCredentialsAndReboot();
/** Boot flow: connect with UI, open portal only if saved creds fail. */
bool wifiSetupConnect();
/** Reconnect using saved creds; never opens the captive portal. */
bool wifiReconnect();
bool wifiBootButtonPressed();
/** GPIO + interrupt setup; call once early in setup(). */
void bootButtonInit();
/** Latched short tap (survives blocking HTTP/display work). */
bool bootButtonConsumeTap();
/** Call after setup WiFi flow so long-press reset cannot interrupt the portal. */
void bootButtonEnableWifiReset();
/** True while the captive setup portal is active. */
bool wifiConfigPortalActive();
/** Call each loop iteration; triggers WiFi reset on long hold. */
void bootButtonPollLongPress();
