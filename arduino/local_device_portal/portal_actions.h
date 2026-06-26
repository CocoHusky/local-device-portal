#pragma once

String bottomActions() {
  bool connected = WiFi.status() == WL_CONNECTED;

  if (!connected && savedSSID.length() == 0) {
    return "";
  }

  String h;

  h += "<div class='card bottom-actions'>";
  h += "<div class='card-title'>Settings</div>";
  h += "<p class='small'>Only use this if you want to change Wi-Fi.</p>";
  h += "<form method='POST' action='/forget'>";
  h += "<button class='btn-danger btn-small' type='submit'>Reset saved Wi-Fi</button>";
  h += "</form>";
  h += "</div>";

  return h;
}
