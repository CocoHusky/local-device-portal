#pragma once

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>

#include "portal_utils.h"

// -- Config -------------------------------------------------------------------

const char* PRODUCT_TITLE = "Wifi Device";
const char* AP_SSID_PREFIX = "WifiDevice";
const char* DEVICE_HOST_PREFIX = "wifi-device";
const char* AP_PASS = "wifi-device-setup";

IPAddress AP_IP(192, 168, 4, 1);
IPAddress AP_MASK(255, 255, 255, 0);

const byte DNS_PORT = 53;

// Clears old saved Wi-Fi once every time a newly compiled sketch is uploaded.
const bool CLEAR_WIFI_ON_NEW_UPLOAD = false;
const char* BUILD_ID = __DATE__ " " __TIME__;

// -- Shared State -------------------------------------------------------------

WebServer server(80);
DNSServer dns;
Preferences prefs;

String savedSSID;
String savedPass;

String setupApSsid;
String deviceHost;
String dashboardUrl;

bool apRunning = false;
bool handoffPending = false;
unsigned long handoffAt = 0;

bool rebootFlag = false;
unsigned long rebootAt = 0;

String onlineSummary = "Offline dashboard mode";
String onlineSource = "local";
String onlineTimezone = "UTC";
bool onlineOk = false;
unsigned long onlineUpdatedAt = 0;
unsigned long lastOnlineFetchAt = 0;
