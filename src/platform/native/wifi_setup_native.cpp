/**
 * Native simulated radio: the station half of platform/wifi_setup.h.
 *
 * There is no Wi-Fi here. "Associated" is a bool, and "has credentials" is a
 * stored SSID — nothing dials a network, because the harness already has the
 * host's connectivity. What this file exists for is the *shape* of the boot
 * flow: the same portal-or-connect decision, the same status screens in the
 * same order, and above all the real shared connecting animation, so the
 * spinner can be designed natively instead of on a 240px panel over USB.
 *
 * The four BOOT-button entry points of that header are not here; they live in
 * button_sdl.cpp, which maps them onto the keyboard. wifiLoop() still calls
 * bootButtonPollLongPress() at exactly the point the device does.
 *
 * Where native necessarily differs from the device:
 *
 *  - Association always succeeds after a fixed animation, so the
 *    statusScreenConnectFailed() path is structurally present but unreachable.
 *  - There is no captive portal and no AP. The portal is a loopback HTTP server
 *    (portal_server.cpp) that the user opens in a browser; portalHints() in
 *    platform_native.cpp already tells the status screen to say so.
 *  - reboot() is a real process exit, so the "reset and reboot" path ends the
 *    program rather than re-entering setup(); see core/platform.h.
 */

#include "platform/wifi_setup.h"

#include <string>

#include "config.h"
#include "core/platform.h"
#include "core/settings.h"
#include "portal_server.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

namespace {

namespace pf = core::platform;

/** Mirrors the device's force-portal flag: same namespace, same key. */
constexpr char kPrefsForcePortalKey[] = "portal";

/**
 * How long the simulated association takes. The device budgets
 * kWifiConnectAttemptMs per try; two seconds is long enough to watch the
 * spinner and short enough not to be in the way of an edit-run cycle.
 */
constexpr unsigned long kConnectAnimationMs = 2000;

bool s_link_up = false;
bool s_force_config_portal = false;

/**
 * wifiLoop() is called from loop() *and* handed to HttpClient::get() as its
 * cooperative poll hook, so it can be entered from inside a call that a
 * previous wifiLoop() is still on the stack of. Serving a portal request from
 * within a portal request would re-enter the accept loop with a half-served
 * connection, so the nested call is simply dropped — the outer pump picks the
 * work up milliseconds later, which no human at a browser can perceive.
 */
bool s_in_wifi_loop = false;

std::string storedSsid() {
  return pf::KeyValueStore::getString(kWifiKvNamespace, kWifiKvSsidKey, "");
}

void markForceConfigPortal() {
  s_force_config_portal = true;
  pf::KeyValueStore::putBool(kWifiKvNamespace, kPrefsForcePortalKey, true);
}

bool consumeForceConfigPortal() {
  const bool pending =
      s_force_config_portal ||
      pf::KeyValueStore::getBool(kWifiKvNamespace, kPrefsForcePortalKey, false);
  if (!pending) {
    return false;
  }
  s_force_config_portal = false;
  pf::KeyValueStore::remove(kWifiKvNamespace, kPrefsForcePortalKey);
  return true;
}

void eraseWifiCredentials() {
  pf::KeyValueStore::remove(kWifiKvNamespace, kWifiKvSsidKey);
  s_link_up = false;
}

/**
 * The point of the whole file: the connecting UI is the real shared code, run
 * at the real frame interval, so what is on the SDL window is what the panel
 * shows. Only the outcome is faked.
 */
void runConnectAnimation(const char* ssid) {
  statusScreenConnectingBegin(ssid);
  const unsigned long deadline = pf::nowMs() + kConnectAnimationMs;
  while (pf::nowMs() < deadline) {
    // Same ordering as waitForLinkWithUi(): the button stays live while the
    // spinner runs, which is how a stuck boot is escaped.
    bootButtonPollLongPress();
    statusScreenConnectingTick();
    pf::sleepMs(config::kWifiConnectingFrameMs);
  }
  s_link_up = true;
}

/**
 * Blocks until the user submits an SSID, mirroring the device's
 * openConfigPortal() loop. Blocking during setup() is the device's behaviour
 * too: nothing else can run until the radar knows where it is.
 */
std::string runPortalUntilCredentials() {
  portalServerStart();
  std::string ssid;
  while (true) {
    bootButtonPollLongPress();
    portalServerPump();
    if (portalServerConsumeCredentials(&ssid) && !ssid.empty()) {
      return ssid;
    }
    pf::sleepMs(10);
  }
}

}  // namespace

bool wifiIsConnected() { return s_link_up; }

bool wifiShowsSetupScreenOnBoot() {
  if (s_force_config_portal) {
    return true;
  }
  return pf::KeyValueStore::getBool(kWifiKvNamespace, kPrefsForcePortalKey,
                                    false);
}

void wifiResetCredentialsAndReboot() {
  markForceConfigPortal();
  eraseWifiCredentials();
  portalServerStop();
  core::settings::clearLocation();
  ui::radar::unitsReset();
  pf::logf("WiFi credentials, location, and units cleared\n");

  statusScreenWifiReset();
  pf::sleepMs(800);
  // exit(0), not a restart: re-entering setup() would carry this process's
  // guard statics over and stop reproducing a cold boot. See core/platform.h.
  pf::reboot();
}

bool wifiSetupConnect() {
  bootButtonInit();
  portalServerStart();

  const bool force_portal = consumeForceConfigPortal();
  if (force_portal) {
    eraseWifiCredentials();
    pf::logf("Opening WiFi setup portal (after reset)\n");
    statusScreenPortal();
    const std::string ssid = runPortalUntilCredentials();
    runConnectAnimation(ssid.c_str());
    pf::logf("Connected: %s  IP %s\n", ssid.c_str(), "127.0.0.1");
    return true;
  }

  pf::logf("Connecting to WiFi (portal opens if needed)...\n");

  std::string ssid = storedSsid();
  if (ssid.empty()) {
    pf::logf("No saved WiFi — opening setup portal\n");
    statusScreenPortal();
    ssid = runPortalUntilCredentials();
  }

  runConnectAnimation(ssid.c_str());
  pf::logf("Connected: %s  IP %s\n", ssid.c_str(), "127.0.0.1");
  return true;
}

bool wifiReconnect() {
  bootButtonInit();
  pf::logf("WiFi reconnecting...\n");
  const std::string ssid = storedSsid();
  runConnectAnimation(ssid.empty() ? "network" : ssid.c_str());
  return true;
}

void wifiLoop() {
  if (s_in_wifi_loop) {
    return;  // See s_in_wifi_loop for why the nested call is dropped.
  }
  s_in_wifi_loop = true;
  // Device order: the button is polled before the portal is serviced, so a
  // long hold wins over a slow request. bootButtonPollLongPress() may not
  // return (long hold -> reset -> exit), which is why nothing below it is
  // required for correctness.
  bootButtonPollLongPress();
  portalServerPump();
  s_in_wifi_loop = false;
}
