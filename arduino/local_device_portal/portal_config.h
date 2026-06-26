#pragma once

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>

// ── Config ────────────────────────────────────────────────────────────────────

const char* AP_SSID = "mmWave-Setup";
const char* AP_PASS = "focusfetch";

IPAddress AP_IP(192, 168, 4, 1);
IPAddress AP_MASK(255, 255, 255, 0);

const byte DNS_PORT = 53;

// Clears old saved Wi-Fi once every time a newly compiled sketch is uploaded.
// Turn false later when you want production behavior.
const bool CLEAR_WIFI_ON_NEW_UPLOAD = true;
const char* BUILD_ID = __DATE__ " " __TIME__;

// ── Shared State ──────────────────────────────────────────────────────────────

WebServer server(80);
DNSServer dns;
Preferences prefs;

String savedSSID;
String savedPass;

String deviceHost;
String dashboardUrl;

bool apRunning = false;
bool handoffPending = false;
unsigned long handoffAt = 0;

bool rebootFlag = false;
unsigned long rebootAt = 0;

String onlineSummary = "Not checked yet";
String onlineSource = "ip-api.com";
String onlineTimezone = "America/Chicago";
bool onlineOk = false;
unsigned long onlineUpdatedAt = 0;
unsigned long lastOnlineFetchAt = 0;
