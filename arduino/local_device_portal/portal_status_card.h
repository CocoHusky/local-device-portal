#pragma once

String statusCard() {
  bool connected = WiFi.status() == WL_CONNECTED;

  String h;

  h += "<div class='card status-card'>";
  h += "<div class='card-title'>Device Status</div>";
  h += "<div class='status-grid'>";

  h += "<div class='status-row'><span class='status-label'>Setup Wi-Fi</span><span class='status-value'>";
  h += htmlEscape(setupApSsid);
  h += "</span></div>";

  h += "<div class='status-row'><span class='status-label'>Setup IP</span><span class='status-value'>192.168.4.1</span></div>";

  h += "<div class='status-row'><span class='status-label'>Local Wi-Fi</span><span class='status-value ";
  h += connected ? "good" : "bad";
  h += "'>";
  h += connected ? "connected" : "not connected";
  h += "</span></div>";

  h += "<div class='status-row'><span class='status-label'>Saved network</span><span class='status-value'>";
  h += savedSSID.length() ? htmlEscape(savedSSID) : "none";
  h += "</span></div>";

  if (connected) {
    h += "<div class='status-row'><span class='status-label'>Dashboard</span><span class='status-value good'>";
    h += htmlEscape(deviceHost + ".local");
    h += "</span></div>";
  }

  h += "</div>";
  h += "</div>";

  return h;
}
