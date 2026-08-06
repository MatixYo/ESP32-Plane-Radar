#include "services/wifi_setup.h"

#include <WiFi.h>
#include <WiFiManager.h>

#include <cstdio>

#include <Preferences.h>
#include <esp_system.h>
#include <esp_wifi.h>

#ifdef WM_MDNS
#include <ESPmDNS.h>
#endif

#include "config.h"
#include "services/radar_location.h"
#include "ui/radar_range.h"
#include "ui/status_screens.h"

portMUX_TYPE s_boot_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool s_boot_tap_pending = false;
volatile bool s_boot_is_down = false;
volatile unsigned long s_boot_down_ms = 0;
bool s_long_press_handled = false;
bool s_boot_interrupt_attached = false;

void IRAM_ATTR onBootButtonIsr() {
  const bool down = digitalRead(config::kBootPin) == LOW;
  const unsigned long now = millis();
  portENTER_CRITICAL_ISR(&s_boot_mux);
  if (down) {
    s_boot_is_down = true;
    s_boot_down_ms = now;
  } else if (s_boot_is_down) {
    const unsigned long held = now - s_boot_down_ms;
    if (held >= config::kBootTapMinMs && held < config::kBootResetHoldMs) {
      s_boot_tap_pending = true;
    }
    s_boot_is_down = false;
  }
  portEXIT_CRITICAL_ISR(&s_boot_mux);
}

void initBootButton() {
  pinMode(config::kBootPin, INPUT_PULLUP);
  if (s_boot_interrupt_attached) {
    return;
  }
  attachInterrupt(digitalPinToInterrupt(static_cast<uint8_t>(config::kBootPin)),
                  onBootButtonIsr, CHANGE);
  s_boot_interrupt_attached = true;
}

namespace {

/** Separate from planeradar prefs (rangeInit) to avoid NVS handle conflicts. */
constexpr char kWifiPrefsNamespace[] = "wifi";
constexpr char kPrefsForcePortalKey[] = "portal";
constexpr char kPrefsSavedNetworkCountKey[] = "net_count";
constexpr char kPrefsSavedNetworkSsidPrefix[] = "net_ssid_";
constexpr char kPrefsSavedNetworkPassPrefix[] = "net_pass_";
constexpr size_t kMaxSavedNetworks = 8;

bool s_force_config_portal = false;
WiFiManager s_wm;
bool s_wm_configured = false;

void ensureWifiManager();
void startLanWebPortal();
void stopLanWebPortal();
bool wifiLinkUp();

constexpr int kCoordParamLen = 20;
constexpr char kCoordInputAttrs[] =
    " type=\"number\" step=\"0.000001\"";

struct SavedWifiNetwork {
  String ssid;
  String password;
};

SavedWifiNetwork s_saved_wifi_networks[kMaxSavedNetworks];
size_t s_saved_wifi_network_count = 0;
bool s_saved_wifi_networks_loaded = false;

WiFiManagerParameter s_param_lat("radar_lat", "Latitude (deg)", "0",
                                kCoordParamLen, kCoordInputAttrs);
WiFiManagerParameter s_param_lon("radar_lon", "Longitude (deg)", "0",
                                kCoordParamLen, kCoordInputAttrs);

char s_miles_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_miles("use_miles", "Display distances in miles", "T", 2,
                                   s_miles_checkbox_attrs, WFM_LABEL_AFTER);

char s_runways_checkbox_attrs[32] = "type=\"checkbox\"";
WiFiManagerParameter s_param_runways("show_runways", "Show airport runways", "T", 2,
                                     s_runways_checkbox_attrs, WFM_LABEL_AFTER);

char s_current_wifi_attrs[16] = " readonly";
WiFiManagerParameter s_param_current_wifi("current_wifi", "Current Wi-Fi", "",
                                          64, s_current_wifi_attrs);

void loadSavedNetworks() {
  if (s_saved_wifi_networks_loaded) {
    return;
  }

  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    s_saved_wifi_networks_loaded = true;
    return;
  }

  s_saved_wifi_network_count = 0;
  const uint8_t count = prefs.getUChar(kPrefsSavedNetworkCountKey, 0);
  for (uint8_t i = 0; i < count && s_saved_wifi_network_count < kMaxSavedNetworks; ++i) {
    const String ssid_key = String(kPrefsSavedNetworkSsidPrefix) + String(i);
    const String pass_key = String(kPrefsSavedNetworkPassPrefix) + String(i);
    const String ssid = prefs.getString(ssid_key.c_str(), "");
    const String pass = prefs.getString(pass_key.c_str(), "");
    if (ssid.length() == 0) {
      continue;
    }
    s_saved_wifi_networks[s_saved_wifi_network_count].ssid = ssid;
    s_saved_wifi_networks[s_saved_wifi_network_count].password = pass;
    ++s_saved_wifi_network_count;
  }

  prefs.end();
  s_saved_wifi_networks_loaded = true;
}

void saveSavedNetworks() {
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, false)) {
    return;
  }

  prefs.putUChar(kPrefsSavedNetworkCountKey,
                 static_cast<uint8_t>(s_saved_wifi_network_count));
  for (size_t i = 0; i < s_saved_wifi_network_count; ++i) {
    const String ssid_key = String(kPrefsSavedNetworkSsidPrefix) + String(i);
    const String pass_key = String(kPrefsSavedNetworkPassPrefix) + String(i);
    prefs.putString(ssid_key.c_str(), s_saved_wifi_networks[i].ssid.c_str());
    prefs.putString(pass_key.c_str(), s_saved_wifi_networks[i].password.c_str());
  }
  for (size_t i = s_saved_wifi_network_count; i < kMaxSavedNetworks; ++i) {
    const String ssid_key = String(kPrefsSavedNetworkSsidPrefix) + String(i);
    const String pass_key = String(kPrefsSavedNetworkPassPrefix) + String(i);
    prefs.remove(ssid_key.c_str());
    prefs.remove(pass_key.c_str());
  }

  prefs.end();
}

void addSavedWifiNetwork(const String& ssid, const String& password) {
  if (ssid.length() == 0) {
    return;
  }

  loadSavedNetworks();
  for (size_t i = 0; i < s_saved_wifi_network_count; ++i) {
    if (s_saved_wifi_networks[i].ssid.equalsIgnoreCase(ssid)) {
      s_saved_wifi_networks[i].password = password;
      if (i > 0) {
        SavedWifiNetwork entry = s_saved_wifi_networks[i];
        for (size_t j = i; j > 0; --j) {
          s_saved_wifi_networks[j] = s_saved_wifi_networks[j - 1];
        }
        s_saved_wifi_networks[0] = entry;
      }
      saveSavedNetworks();
      return;
    }
  }

  if (s_saved_wifi_network_count < kMaxSavedNetworks) {
    s_saved_wifi_networks[s_saved_wifi_network_count] = SavedWifiNetwork{ssid, password};
    ++s_saved_wifi_network_count;
  } else {
    for (size_t i = kMaxSavedNetworks - 1; i > 0; --i) {
      s_saved_wifi_networks[i] = s_saved_wifi_networks[i - 1];
    }
    s_saved_wifi_networks[0] = SavedWifiNetwork{ssid, password};
  }

  saveSavedNetworks();
}

void clearSavedWifiNetworks() {
  s_saved_wifi_network_count = 0;
  s_saved_wifi_networks_loaded = true;
  for (size_t i = 0; i < kMaxSavedNetworks; ++i) {
    s_saved_wifi_networks[i].ssid = "";
    s_saved_wifi_networks[i].password = "";
  }

  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, false)) {
    return;
  }
  prefs.remove(kPrefsSavedNetworkCountKey);
  for (size_t i = 0; i < kMaxSavedNetworks; ++i) {
    const String ssid_key = String(kPrefsSavedNetworkSsidPrefix) + String(i);
    const String pass_key = String(kPrefsSavedNetworkPassPrefix) + String(i);
    prefs.remove(ssid_key.c_str());
    prefs.remove(pass_key.c_str());
  }
  prefs.end();
}

String buildCurrentWifiShareText() {
  String share_text;
  if (WiFi.status() == WL_CONNECTED) {
    share_text = WiFi.SSID();
    share_text += " @ ";
    share_text += WiFi.localIP().toString();
  } else {
    share_text = "Not connected";
  }

  if (WiFi.status() == WL_CONNECTED) {
    const String password = WiFi.psk();
    share_text += password.length() > 0 ? " | password available" : " | password unavailable";
  }
  return share_text;
}

void rememberCurrentWifiNetwork() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  const String ssid = WiFi.SSID();
  if (ssid.length() == 0) {
    return;
  }

  const String password = WiFi.psk();
  addSavedWifiNetwork(ssid, password);
  Serial.printf("Saved Wi-Fi profile: %s\n", ssid.c_str());
}

void refreshPortalParamDefaults() {
  char lat_buf[kCoordParamLen + 1];
  char lon_buf[kCoordParamLen + 1];
  snprintf(lat_buf, sizeof(lat_buf), "%.6f", services::location::lat());
  snprintf(lon_buf, sizeof(lon_buf), "%.6f", services::location::lon());
  s_param_lat.setValue(lat_buf, kCoordParamLen);
  s_param_lon.setValue(lon_buf, kCoordParamLen);
  snprintf(s_miles_checkbox_attrs, sizeof(s_miles_checkbox_attrs), "type=\"checkbox\"%s",
           ui::radar::useMiles() ? " checked" : "");
  s_param_miles.setValue("T", 2);
  snprintf(s_runways_checkbox_attrs, sizeof(s_runways_checkbox_attrs),
           "type=\"checkbox\"%s", ui::radar::showRunways() ? " checked" : "");
  s_param_runways.setValue("T", 2);
  const String current_wifi = buildCurrentWifiShareText();
  s_param_current_wifi.setValue(current_wifi.c_str(), current_wifi.length());
}

void onPortalParamsSaved() {
  if (!services::location::saveFromStrings(s_param_lat.getValue(),
                                           s_param_lon.getValue())) {
    Serial.println("Invalid lat/lon in portal — keeping previous location");
  }
  ui::radar::saveMilesFromPortal(s_param_miles.getValue());
  ui::radar::saveRunwaysFromPortal(s_param_runways.getValue());
}

void attachPortalParams(WiFiManager& wm) {
  refreshPortalParamDefaults();
  wm.addParameter(&s_param_lat);
  wm.addParameter(&s_param_lon);
  wm.addParameter(&s_param_miles);
  wm.addParameter(&s_param_runways);
  wm.addParameter(&s_param_current_wifi);
  wm.setSaveParamsCallback(onPortalParamsSaved);
}

void markForceConfigPortal() {
  s_force_config_portal = true;
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, false)) {
    return;
  }
  prefs.putBool(kPrefsForcePortalKey, true);
  prefs.end();
}

bool consumeForceConfigPortal() {
  if (s_force_config_portal) {
    s_force_config_portal = false;
    Preferences prefs;
    if (prefs.begin(kWifiPrefsNamespace, false)) {
      prefs.remove(kPrefsForcePortalKey);
      prefs.end();
    }
    return true;
  }

  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    return false;
  }
  const bool pending = prefs.getBool(kPrefsForcePortalKey, false);
  prefs.end();
  if (!pending) {
    return false;
  }

  if (prefs.begin(kWifiPrefsNamespace, false)) {
    prefs.remove(kPrefsForcePortalKey);
    prefs.end();
  }
  return true;
}

bool storedWifiCredentials() {
  loadSavedNetworks();
  return s_saved_wifi_network_count > 0;
}

void eraseWifiCredentials() {
  stopLanWebPortal();
  WiFi.setAutoReconnect(false);
  WiFi.mode(WIFI_OFF);
  delay(100);

  clearSavedWifiNetworks();
  ensureWifiManager();
  WiFi.persistent(true);
  s_wm.resetSettings();
  s_wm.erase();
  WiFi.disconnect(true, true);
  WiFi.persistent(false);

  WiFi.mode(WIFI_OFF);
  delay(100);
}

void resetWifiCredentials() {
  markForceConfigPortal();
  eraseWifiCredentials();
  services::location::clear();
  ui::radar::unitsReset();
  Serial.println("WiFi credentials, location, and units cleared");
}

void onConfigPortalApStarted(WiFiManager*) {
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  statusScreenPortal();
#ifdef WM_MDNS
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("Setup portal: http://%s.local (or http://%s)\n",
                  config::kPortalHostname, config::kPortalIp);
  } else {
    Serial.printf("Setup portal: http://%s (mDNS unavailable)\n", config::kPortalIp);
  }
#else
  Serial.printf("Setup portal: http://%s\n", config::kPortalIp);
#endif
}

bool wifiLinkUp() {
  return WiFi.status() == WL_CONNECTED &&
         WiFi.localIP() != IPAddress(0, 0, 0, 0);
}

void ensureWifiManager() {
  if (s_wm_configured) {
    return;
  }
  s_wm.setConfigPortalTimeout(config::kWifiPortalTimeoutSec);
  s_wm.setAPStaticIPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1),
                           IPAddress(255, 255, 255, 0));
  s_wm.setHostname(config::kPortalHostname);
  s_wm.setAPCallback(onConfigPortalApStarted);
  attachPortalParams(s_wm);
  s_wm_configured = true;
}

void startLanWebPortal() {
  if (!wifiLinkUp() || s_wm.getWebPortalActive() ||
      s_wm.getConfigPortalActive()) {
    return;
  }
  refreshPortalParamDefaults();
  WiFi.mode(WIFI_STA);
  s_wm.setConfigPortalBlocking(false);
#ifdef WM_MDNS
  MDNS.end();
  if (MDNS.begin(config::kPortalHostname)) {
    MDNS.addService("http", "tcp", 80);
  }
#endif
  s_wm.startWebPortal();
  Serial.printf("LAN config: http://%s.local or http://%s\n",
                config::kPortalHostname, WiFi.localIP().toString().c_str());
}

void stopLanWebPortal() {
  if (!s_wm.getWebPortalActive()) {
    return;
  }
  s_wm.stopWebPortal();
#ifdef WM_MDNS
  MDNS.end();
#endif
}

void prepareSta() {
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.setAutoReconnect(true);
}

void startStaConnect(const String& ssid, const String& pass) {
  prepareSta();
  if (ssid.length() > 0) {
    WiFi.begin(ssid.c_str(), pass.c_str());
  } else {
    WiFi.begin();
  }
}

bool waitForLinkWithUi(const char* ssid_for_ui, unsigned long attempt_ms) {
  const unsigned long deadline = millis() + attempt_ms;
  while (millis() < deadline) {
    if (wifiLinkUp()) {
      return true;
    }
    bootButtonPollLongPress();
    statusScreenConnectingTick();
    delay(config::kWifiConnectingFrameMs);
  }
  return wifiLinkUp();
}

bool tryConnectWithUi(const String& ssid, const String& pass, bool show_ui) {
  if (wifiLinkUp()) {
    return true;
  }

  const char* ui_ssid = ssid.length() > 0 ? ssid.c_str() : "network";
  if (show_ui) {
    statusScreenConnectingBegin(ui_ssid);
  }

  for (uint8_t attempt = 1; attempt <= config::kWifiConnectAttempts; ++attempt) {
    if (attempt > 1) {
      Serial.printf("WiFi connect retry %u/%u\n", attempt,
                    config::kWifiConnectAttempts);
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      delay(400);
    }

    startStaConnect(ssid, pass);

    if (waitForLinkWithUi(ui_ssid, config::kWifiConnectAttemptMs)) {
      return true;
    }
  }

  return false;
}

bool connectSavedNetwork(bool show_ui) {
  if (!storedWifiCredentials()) {
    return false;
  }

  ensureWifiManager();
  loadSavedNetworks();
  for (size_t i = 0; i < s_saved_wifi_network_count; ++i) {
    const SavedWifiNetwork& network = s_saved_wifi_networks[i];
    if (network.ssid.length() == 0) {
      continue;
    }
    if (tryConnectWithUi(network.ssid, network.password, show_ui)) {
      rememberCurrentWifiNetwork();
      return true;
    }
  }
  return false;
}

bool openConfigPortal() {
  stopLanWebPortal();
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  delay(50);
  statusScreenPortal();
  s_wm.setConfigPortalBlocking(false);
  s_wm.startConfigPortal(config::kPortalApName);
  while (s_wm.getConfigPortalActive()) {
    bootButtonPollLongPress();
    if (s_wm.process()) {
      return true;
    }
    delay(10);
  }
  return wifiLinkUp();
}

}  // namespace

bool wifiShowsSetupScreenOnBoot() {
  if (s_force_config_portal) {
    return true;
  }
  Preferences prefs;
  if (!prefs.begin(kWifiPrefsNamespace, true)) {
    return false;
  }
  const bool pending = prefs.getBool(kPrefsForcePortalKey, false);
  prefs.end();
  return pending;
}

bool wifiBootButtonPressed() {
  return digitalRead(config::kBootPin) == LOW;
}

void bootButtonInit() { initBootButton(); }

bool bootButtonConsumeTap() {
  portENTER_CRITICAL(&s_boot_mux);
  const bool tap = s_boot_tap_pending;
  if (tap) {
    s_boot_tap_pending = false;
  }
  portEXIT_CRITICAL(&s_boot_mux);
  return tap;
}

void bootButtonPollLongPress() {
  if (wifiBootButtonPressed()) {
    portENTER_CRITICAL(&s_boot_mux);
    if (!s_boot_is_down) {
      s_boot_is_down = true;
      s_boot_down_ms = millis();
    }
    const unsigned long down_ms = s_boot_down_ms;
    portEXIT_CRITICAL(&s_boot_mux);

    if (!s_long_press_handled &&
        millis() - down_ms >= config::kBootResetHoldMs) {
      s_long_press_handled = true;
      Serial.println("BOOT held — resetting WiFi");
      wifiResetCredentialsAndReboot();
    }
  } else {
    portENTER_CRITICAL(&s_boot_mux);
    s_boot_is_down = false;
    portEXIT_CRITICAL(&s_boot_mux);
    s_long_press_handled = false;
  }
}

void wifiResetCredentialsAndReboot() {
  resetWifiCredentials();
  statusScreenWifiReset();
  delay(800);
  esp_restart();
}

bool wifiReconnect() {
  initBootButton();
  Serial.println("WiFi reconnecting...");
  return connectSavedNetwork(true);
}

void wifiLoop() {
  ensureWifiManager();
  if (wifiLinkUp()) {
    if (!s_wm.getWebPortalActive() && !s_wm.getConfigPortalActive()) {
      startLanWebPortal();
    }
    if (s_wm.getWebPortalActive() || s_wm.getConfigPortalActive()) {
      bootButtonPollLongPress();
      s_wm.process();
    }
  } else {
    stopLanWebPortal();
  }
}

bool wifiSetupConnect() {
  initBootButton();
  ensureWifiManager();

  const bool force_portal = consumeForceConfigPortal();
  WiFi.setAutoReconnect(false);

  if (force_portal) {
    eraseWifiCredentials();
    WiFi.mode(WIFI_OFF);
    delay(100);
  }

  if (force_portal) {
    Serial.println("Opening WiFi setup portal (after reset)");
    if (openConfigPortal() && wifiLinkUp()) {
      WiFi.setAutoReconnect(true);
      rememberCurrentWifiNetwork();
      Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                    WiFi.localIP().toString().c_str());
      return true;
    }
    Serial.println("WiFi connection failed");
    statusScreenConnectFailed();
    return false;
  }

  Serial.println("Connecting to WiFi (portal opens if needed)...");

  if (wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  if (storedWifiCredentials() && connectSavedNetwork(true)) {
    WiFi.setAutoReconnect(true);
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  if (storedWifiCredentials()) {
    Serial.println("Saved WiFi could not connect — opening setup portal");
  } else {
    Serial.println("No saved WiFi — opening setup portal");
  }

  if (openConfigPortal() && wifiLinkUp()) {
    WiFi.setAutoReconnect(true);
    rememberCurrentWifiNetwork();
    Serial.printf("Connected: %s  IP %s\n", WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
  }

  Serial.println("WiFi connection failed");
  statusScreenConnectFailed();
  return false;
}
