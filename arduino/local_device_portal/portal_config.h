#pragma once

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>

#include "portal_utils.h"

// ── Config ────────────────────────────────────────────────────────────────────

const char* PRODUCT_TITLE = "Wifi Device";
const char* AP_SSID_PREFIX = "WifiDevice";
const char* DEVICE_HOST_PREFIX = "wifi-device";
const char* AP_PASS = "wifi-device-setup";

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
// the setup Wi-Fi, browser-side calls to public APIs can fail even though the ESP32
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

String setupApSsid;
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

int portalSkipJsonWhitespace(const String& body, int pos) {
  while (pos < (int)body.length()) {
    char c = body[pos];
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      pos++;
    } else {
      break;
    }
  }

  return pos;
}

String portalJsonString(const String& body, const String& key) {
  String marker = "\"" + key + "\":";
  int searchAt = 0;

  while (true) {
    int start = body.indexOf(marker, searchAt);
    if (start < 0) return "";

    start += marker.length();
    start = portalSkipJsonWhitespace(body, start);

    if (start < (int)body.length() && body[start] == '"') {
      start++;
      String out;

      while (start < (int)body.length()) {
        char c = body[start++];

        if (c == '\\' && start < (int)body.length()) {
          char escaped = body[start++];
          if (escaped == 'n') out += '\n';
          else if (escaped == 'r') out += '\r';
          else if (escaped == 't') out += '\t';
          else out += escaped;
        } else if (c == '"') {
          return out;
        } else {
          out += c;
        }
      }

      return "";
    }

    searchAt = start + 1;
  }
}

String portalJsonNumber(const String& body, const String& key) {
  String marker = "\"" + key + "\":";
  int searchAt = 0;

  while (true) {
    int start = body.indexOf(marker, searchAt);
    if (start < 0) return "";

    start += marker.length();
    start = portalSkipJsonWhitespace(body, start);

    if (start < (int)body.length()) {
      char first = body[start];

      if ((first >= '0' && first <= '9') || first == '-' || first == '.') {
        int end = start;

        while (end < (int)body.length()) {
          char c = body[end];

          if ((c >= '0' && c <= '9') || c == '-' || c == '.' || c == 'e' || c == 'E' || c == '+') {
            end++;
          } else {
            break;
          }
        }

        return body.substring(start, end);
      }
    }

    // Some APIs include a string unit entry before the numeric data entry, for
    // example Open-Meteo current_units.temperature_2m = "°F". Keep looking for
    // the later numeric value instead of treating that as a missing number.
    searchAt = start + 1;
  }
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

const char* portalPosixTimezone(const String& tz) {
  if (tz == "America/Chicago") return "CST6CDT,M3.2.0/2,M11.1.0/2";
  if (tz == "America/New_York" || tz == "America/Detroit" || tz == "America/Toronto") return "EST5EDT,M3.2.0/2,M11.1.0/2";
  if (tz == "America/Denver") return "MST7MDT,M3.2.0/2,M11.1.0/2";
  if (tz == "America/Phoenix") return "MST7";
  if (tz == "America/Los_Angeles") return "PST8PDT,M3.2.0/2,M11.1.0/2";
  if (tz == "America/Anchorage") return "AKST9AKDT,M3.2.0/2,M11.1.0/2";
  if (tz == "Pacific/Honolulu") return "HST10";
  if (tz == "UTC" || tz == "Etc/UTC") return "UTC0";

  // Fallback keeps the dashboard from failing if ip-api returns an IANA zone
  // that is not in this small embedded map.
  return "UTC0";
}

String portalFormattedDeviceTime(const String& tz) {
  const char* tzRule = portalPosixTimezone(tz);
  configTzTime(tzRule, "pool.ntp.org", "time.nist.gov", "time.google.com");

  struct tm timeinfo;
  if (!getLocalTime(&timeinfo, 4500)) {
    return "";
  }

  char buf[40];
  strftime(buf, sizeof(buf), "%Y-%m-%d %I:%M:%S %p", &timeinfo);

  String out = String(buf);
  if (tz.length()) out += " " + tz;
  return out;
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

  String geoBody;
  if (!portalHttpGet("http://ip-api.com/json/?fields=status,message,country,regionName,city,timezone,lat,lon,query", geoBody)) {
    onlineWeather = "location lookup failed";
    onlineDeviceTime = "location/time lookup failed";
    return;
  }

  String geoStatus = portalJsonString(geoBody, "status");
  if (geoStatus != "success") {
    onlineWeather = "location lookup returned an error";
    onlineDeviceTime = "location/time lookup returned an error";
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

  String tz = onlineTimezone.length() ? onlineTimezone : String("America/Chicago");
  String ntpTime = portalFormattedDeviceTime(tz);

  if (ntpTime.length()) {
    onlineDeviceTime = ntpTime;
  } else {
    String timeBody;
    String timeUrl = "http://worldtimeapi.org/api/timezone/" + tz;

    if (portalHttpGet(timeUrl, timeBody)) {
      String dt = portalJsonString(timeBody, "datetime");
      if (!dt.length()) dt = portalJsonString(timeBody, "utc_datetime");
      onlineDeviceTime = dt.length() ? dt : "time response missing datetime";
    } else {
      onlineDeviceTime = "device NTP and time API request failed";
    }
  }

  if (!lat.length() || !lon.length()) {
    onlineWeather = "location missing lat/lon";
    return;
  }

  String weatherUrl = "https://api.open-meteo.com/v1/forecast?latitude=" + lat +
                      "&longitude=" + lon +
                      "&current=temperature_2m,apparent_temperature,weather_code,wind_speed_10m" +
                      "&current_weather=true" +
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

  // Fallback for Open-Meteo's compact current_weather object.
  if (!temp.length()) temp = portalJsonNumber(weatherBody, "temperature");
  if (!wind.length()) wind = portalJsonNumber(weatherBody, "windspeed");
  if (!code.length()) code = portalJsonNumber(weatherBody, "weathercode");

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

String dashboardCopyControls() {
  String h;
  h += "<input id='dashUrl' readonly value='" + dashboardUrl + "' ";
  h += "onclick='this.select()' style='font-size:.9rem;font-weight:800;color:#fff'>";
  h += "<button class='btn-primary' type='button' onclick='copyDashUrl()'>Copy dashboard URL</button>";
  h += String("<p class='small'>After ") + htmlEscape(setupApSsid) + " disappears, reconnect to your normal Wi-Fi, open a new browser tab, and paste this address.</p>";
  h += "<script>function copyDashUrl(){var i=document.getElementById('dashUrl');if(!i)return;i.select();i.setSelectionRange(0,9999);if(navigator.clipboard){navigator.clipboard.writeText(i.value);}else{document.execCommand('copy');}}</script>";
  return h;
}

void removeDashboardAutoRedirects(String& body) {
  int metaStart = body.indexOf("<meta http-equiv='refresh'");
  if (metaStart >= 0) {
    int metaEnd = body.indexOf(">", metaStart);
    if (metaEnd > metaStart) body.remove(metaStart, metaEnd - metaStart + 1);
  }

  int scriptStart = body.indexOf("<script>var dashboardUrl=");
  if (scriptStart >= 0) {
    int scriptEnd = body.indexOf("</script>", scriptStart);
    if (scriptEnd > scriptStart) body.remove(scriptStart, scriptEnd - scriptStart + 9);
  }
}

void patchSetupHandoffHtml(String& body) {
  // Keep setup flow in the same captive-portal tab. Do not try to auto-open the
  // local dashboard from the setup AP because captive portals often block it.
  body.replace("target='_blank' rel='noopener' href='", "href='");

  body.replace("Click below. A dashboard tab will open, the setup Wi-Fi will turn off, then your device should return to your normal Wi-Fi.",
               "Click below to close the setup Wi-Fi. Copy the dashboard address first, then reconnect to your normal Wi-Fi and paste it into a browser.");
  body.replace(">Go to dashboard</a>", ">Finish setup and close setup Wi-Fi</a>");

  if (body.indexOf("Opening Dashboard") < 0 && body.indexOf("Opening dashboard") < 0) {
    return;
  }

  removeDashboardAutoRedirects(body);

  body.replace("<div class='card-title'>Opening dashboard</div>", "<div class='card-title'>Finish setup</div>");
  body.replace("<h2>Switching Wi-Fi</h2>", "<h2>Setup Wi-Fi closing</h2>");
  body.replace("<div class='spinner'></div>", "");
  body.replace("<div class='countdown'>15 sec</div>", "");
  body.replace("The setup Wi-Fi is turning off. Your device should reconnect to your normal Wi-Fi, then open the dashboard.",
               "The setup Wi-Fi is turning off. Captive portal browsers can block automatic dashboard redirects, so this page will not redirect.");
  body.replace("This can take a few seconds.",
               String("Copy the dashboard URL below. When ") + setupApSsid + " disconnects, reconnect to your normal Wi-Fi and paste the URL into a normal browser tab.");

  String openNow = "<a class='btn btn-primary' href='" + dashboardUrl + "'>Open dashboard now</a>";
  body.replace(openNow, dashboardCopyControls());
}

String patchDashboardHtml(String body) {
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
               "setOnline((d.online_ok&&d.online_enriched_ok)?'ok':'info',deviceOnlineRows(d));");

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

  if (type.indexOf("text/html") >= 0) {
    patchSetupHandoffHtml(body);

    if (body.indexOf("Browser online time request failed") >= 0) {
      body = patchDashboardHtml(body);
    }
  } else if (type.indexOf("application/json") >= 0 && body.indexOf("\"online_summary\"") >= 0) {
    body = patchDataJson(body);
  }

  WebServer::send(code, content_type, body);
}
