#pragma once

/**
 * Private seam between the native simulated radio and its local config portal.
 *
 * Not part of the portable API in include/ — both ends live in
 * src/platform/native/ and nothing else may depend on this. The device has no
 * counterpart: there WiFiManager owns both the AP and the form.
 */

#include <string>

/**
 * Storage identity of the simulated station credentials.
 *
 * Shared here because two files write it: the portal saves the submitted SSID
 * and wifi_setup_native.cpp reads it to decide whether credentials exist. The
 * namespace matches the device's own Wi-Fi preferences namespace.
 */
constexpr char kWifiKvNamespace[] = "wifi";
constexpr char kWifiKvSsidKey[] = "ssid";

/**
 * Bind 127.0.0.1:8080 and start listening. Idempotent.
 *
 * Returns false if the port could not be bound (typically a second harness
 * already running); the caller carries on without a portal rather than dying.
 */
bool portalServerStart();

/**
 * Accept and serve a bounded amount of work, then return. Never blocks waiting
 * for a client. Safe to call when the server was never started.
 */
void portalServerPump();

/** Close the listening socket. Idempotent. */
void portalServerStop();

/**
 * Latched notification that a form submission supplied a non-empty SSID.
 *
 * Returns true at most once per submission and writes the SSID to `ssid`.
 * Latched rather than polled from storage so a resubmission of the same SSID
 * still ends the boot-time portal wait.
 */
bool portalServerConsumeCredentials(std::string* ssid);
