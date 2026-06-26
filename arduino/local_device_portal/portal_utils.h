#pragma once

// ── Utility ───────────────────────────────────────────────────────────────────

String htmlEscape(const String& s) {
  String out;
  out.reserve(s.length() + 8);

  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];

    if (c == '&') out += "&amp;";
    else if (c == '<') out += "&lt;";
    else if (c == '>') out += "&gt;";
    else if (c == '"') out += "&quot;";
    else if (c == '\'') out += "&#39;";
    else out += c;
  }

  return out;
}

String jsonEscape(const String& s) {
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

int signalPercent(int rssi) {
  int pct = 2 * (rssi + 100);  // -90 ≈ weak, -30 ≈ strong

  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;

  return pct;
}

String signalLabel(int rssi) {
  if (rssi >= -55) return "Excellent";
  if (rssi >= -67) return "Good";
  if (rssi >= -75) return "Fair";
  return "Weak";
}
