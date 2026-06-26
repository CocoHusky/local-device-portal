#pragma once

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

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

// ── Dashboard response patching ───────────────────────────────────────────────
//
// The setup AP is intentionally not a router. When a phone/laptop is still on
// mmWave-Setup, browser-side calls to public APIs can fail even though the ESP32
// itself is online through STA Wi-Fi. This server wrapper keeps dashboard checks
// device-side: /data is enriched by the ESP32, and dashboard JavaScript renders
// those local fields instead of calling public APIs from the browser.

class PortalWebServer : public WebServer {
 public:
  explicit PortalWebServer(int port) : WebServer(port) {}
  using WebServer::send;
  void send(int code, const char* content_type, const String& content);
};

// ── Shared State ──────────────────────────────────────────────────────────────

PortalWebServer server(80);
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

String onlineDeviceTime = "not checked yet";
String onlineWeather = "not checked yet";
String onlineWeatherLocation = "unknown";
String onlineWeatherSource = "open-meteo.com";
bool onlineEnrichedOk = false;
unsigned long lastOnlineEnrichedFetchAt = 0;

// ── Small local helpers for response patching ─────────────────────────────────

String portalJsonEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);

  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"') out += "\\\"";
    else if (c == '\\') out += "\\\\";
    else if (c == '\n') out += "\\n";
    else if (c == '\r') out += "\\r";
    else out += c;
  }

  return out;
}

String portalJsonString(const String& body, const String& key) {
  String marker = "\"" + key + "\":\"";
  int start = body.indexOf(marker);
  if (start < 0) return "";

  start += marker.length();
  int end = body.indexOf('"', start);
  if (end < 0) return "";

  return body.substring(start, end);
}

String portalJsonNumber(const String& body, const String& key) {
  String marker = "\"" + key + "\":";
  int start = body.indexOf(marker);
  if (start < 0) return "";

  start += marker.length();
  int end = start;

  while (end < (int)body.length()) {
    char c = body[end];
    if ((c >= '0' && c <= '9') || c == '-' || c == '.') {
      end++;
    } else {
      break;
    }
  }

  return body.substring(start, end);
}

String portalWeatherText(int code) {
  if (code == 0) return "clear";
  if (code == 1 || code == 2 || code == 3) return "partly cloudy";
  if (code == 45 || code == 48) return "fog";
  if ((code >= 51 && code <= 57) || (code >= 61 && code <= 67) ||
      (code >= 80 && code <= 82)) return "rain";
  if ((code >= 71 && code <= 77) || (code >= 85 && code <= 86)) return "snow";
  if (code >= 95 && code <= 99) return "thunderstorm";
  return "weather code " + String(code);
}

bool portalHttpGet(const String& url, String& body, unsigned long timeoutMs = 5500) {
  HTTPClient http;
  http.setConnectTimeout(timeoutMs);
  http.setTimeout(timeoutMs);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  WiFiClientSecure secureClient;
  bool ok = false;

  if (url.startsWith("https://")) {
    secureClient.setInsecure();
    ok = http.begin(secureClient, url);
  } else {
    ok = http.begin(url);
  }

  if (!ok) {
    return false;
  }

  int code = http.GET();
  if (code == HTTP_CODE_OK) {
    body = http.getString();
    http.end();
    return true;
  }

  body = "HTTP " + String(code);
  http.end();
  return false;
}

void enrichOnlineDashboardData(bool force = false) {
  if (WiFi.status() != WL_CONNECTED) {
    onlineEnrichedOk = false;
    onlineDeviceTime = "local Wi-Fi is not connected";
    onlineWeather = "not available";
    return;
  }

  if (!force && millis() - lastOnlineEnrichedFetchAt < 60000) {
    return;
  }

  lastOnlineEnrichedFetchAt = millis();
  onlineEnrichedOk = false;

  String tz = onlineTimezone.length() ? onlineTimezone : String("America/Chicago");
  String timeBody;
  String timeUrl = "https://worldtimeapi.org/api/timezone/" + tz;

  if (portalHttpGet(timeUrl, timeBody)) {
    String dt = portalJsonString(timeBody, "datetime");
    if (!dt.length()) dt = portalJsonString(timeBody, "utc_datetime");
    onlineDeviceTime = dt.length() ? dt : "time response missing datetime";
    onlineEnrichedOk = dt.length() > 0;
  } else {
    onlineDeviceTime = "device time request failed";
  }

  String geoBody;
  if (!portalHttpGet("http://ip-api.com/json/?fields=status,message,country,regionName,city,timezone,lat,lon,query", geoBody)) {
    onlineWeather = "location lookup failed";
    return;
  }

  String geoStatus = portalJsonString(geoBody, "status");
  if (geoStatus != "success") {
    onlineWeather = "location lookup returned an error";
    return;
  }

  String lat = portalJsonNumber(geoBody, "lat");
  String lon = portalJsonNumber(geoBody, "lon");
  String city = portalJsonString(geoBody, "city");
  String region = portalJsonString(geoBody, "regionName");
  String geoTz = portalJsonString(geoBody, "timezone");

  if (geoTz.length()) onlineTimezone = geoTz;

  onlineWeatherLocation = city;
  if (region.length()) {
    if (onlineWeatherLocation.length()) onlineWeatherLocation += ", ";
    onlineWeatherLocation += region;
  }
  if (!onlineWeatherLocation.length()) onlineWeatherLocation = "detected location";

  if (!lat.length() || !lon.length()) {
    onlineWeather = "location missing lat/lon";
    return;
  }

  String weatherUrl = "https://api.open-meteo.com/v1/forecast?latitude=" + lat +
                      "&longitude=" + lon +
                      "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m" +
                      "&temperature_unit=fahrenheit&wind_speed_unit=mph&timezone=auto";

  String weatherBody;
  if (!portalHttpGet(weatherUrl, weatherBody)) {
    onlineWeather = "weather request failed";
    return;
  }

  String temp = portalJsonNumber(weatherBody, "temperature_2m");
  String feels = portalJsonNumber(weatherBody, "apparent_temperature");
  String wind = portalJsonNumber(weatherBody, "wind_speed_10m");
  String code = portalJsonNumber(weatherBody, "weather_code");

  if (!temp.length()) {
    onlineWeather = "weather response missing temperature";
    return;
  }

  onlineWeather = temp + " F";
  if (feels.length()) onlineWeather += " / feels " + feels + " F";
  if (code.length()) onlineWeather += " / " + portalWeatherText(code.toInt());
  if (wind.length()) onlineWeather += " / wind " + wind + " mph";
  onlineEnrichedOk = true;
}

String patchDashboardHtml(String body) {
  // Keep the handoff in one tab: /handoff turns off setup AP, then the OS should
  // reconnect to the saved Wi-Fi and the page redirects to the local dashboard.
  body.replace("target='_blank' rel='noopener' href='", "href='");

  int start = body.indexOf("function fetchInternetTime");
  int end = body.indexOf("function refreshData", start);

  if (start >= 0 && end > start) {
    String replacement;
    replacement += "function deviceOnlineRows(d){";
    replacement += "var rows=[d.online_summary||'Device internet check pending'];";
    replacement += "rows.push(['Device internet time',d.device_time||'not available']);";
    replacement += "rows.push(['Timezone',d.online_timezone||'unknown']);";
    replacement += "rows.push(['Weather',d.weather||'not available']);";
    replacement += "rows.push(['Weather location',d.weather_location||'unknown']);";
    replacement += "return rows;}";

    body = body.substring(0, start) + replacement + body.substring(end);
  }

  body.replace("if(d.online_ok){fetchInternetTime(d.online_timezone,d.online_summary);}else{browserOnlineFallback(d.online_summary,d.online_timezone);}",
               "setOnline(d.online_ok?'ok':'err',deviceOnlineRows(d));");

  return body;
}

String patchDataJson(String body) {
  if (body.indexOf("\"online_summary\"") < 0) {
    return body;
  }

  enrichOnlineDashboardData(false);

  int end = body.lastIndexOf('}');
  if (end < 0) return body;

  String extra;
  extra += ",\"device_time\":\"" + portalJsonEscape(onlineDeviceTime) + "\"";
  extra += ",\"weather\":\"" + portalJsonEscape(onlineWeather) + "\"";
  extra += ",\"weather_location\":\"" + portalJsonEscape(onlineWeatherLocation) + "\"";
  extra += ",\"weather_source\":\"" + portalJsonEscape(onlineWeatherSource) + "\"";
  extra += ",\"online_enriched_ok\":" + String(onlineEnrichedOk ? "true" : "false");

  return body.substring(0, end) + extra + body.substring(end);
}

void PortalWebServer::send(int code, const char* content_type, const String& content) {
  String type = content_type ? String(content_type) : String("");
  String body = content;

  if (type.indexOf("text/html") >= 0 && body.indexOf("Browser online time request failed") >= 0) {
    body = patchDashboardHtml(body);
  } else if (type.indexOf("application/json") >= 0 && body.indexOf("\"online_summary\"") >= 0) {
    body = patchDataJson(body);
  }

  WebServer::send(code, content_type, body);
}
