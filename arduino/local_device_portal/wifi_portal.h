#pragma once

// ── Hostname / mDNS ───────────────────────────────────────────────────────────

void setupHostname() {
  uint64_t mac = ESP.getEfuseMac();

  char suffix[7];
  snprintf(suffix, sizeof(suffix), "%06X", (uint32_t)(mac & 0xFFFFFF));

  setupApSsid = String(AP_SSID_PREFIX) + "-" + String(suffix);

  deviceHost = "mmwave-" + String(suffix);
  deviceHost.toLowerCase();

  dashboardUrl = "http://" + deviceHost + ".local/";

  Serial.print("Setup Wi-Fi: ");
  Serial.println(setupApSsid);
  Serial.print("Device host: ");
  Serial.println(deviceHost);
}

void startMDNS() {
  if (WiFi.status() != WL_CONNECTED) return;

  MDNS.end();

  if (MDNS.begin(deviceHost.c_str())) {
    MDNS.addService("http", "tcp", 80);
    dashboardUrl = "http://" + deviceHost + ".local/";

    Serial.print("mDNS dashboard: ");
    Serial.println(dashboardUrl);
  } else {
    dashboardUrl = "http://" + WiFi.localIP().toString() + "/";
    Serial.println("mDNS failed. Using IP fallback.");
  }
}

// ── Credentials ───────────────────────────────────────────────────────────────

void clearWifiIfNewUpload() {
  if (!CLEAR_WIFI_ON_NEW_UPLOAD) return;

  Preferences sys;
  sys.begin("sys", false);

  String lastBuild = sys.getString("build", "");

  if (lastBuild != String(BUILD_ID)) {
    Serial.println("New firmware upload detected.");
    Serial.println("Clearing saved Wi-Fi credentials.");

    Preferences wifi;
    wifi.begin("wifi", false);
    wifi.clear();
    wifi.end();

    savedSSID = "";
    savedPass = "";

    sys.putString("build", BUILD_ID);
  }

  sys.end();
}

void loadCreds() {
  prefs.begin("wifi", true);
  savedSSID = prefs.getString("ssid", "");
  savedPass = prefs.getString("pass", "");
  prefs.end();
}

void saveCreds(const String& ssid, const String& pass) {
  prefs.begin("wifi", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();

  savedSSID = ssid;
  savedPass = pass;
}

void forgetCredsOnly() {
  prefs.begin("wifi", false);
  prefs.clear();
  prefs.end();

  savedSSID = "";
  savedPass = "";
}
