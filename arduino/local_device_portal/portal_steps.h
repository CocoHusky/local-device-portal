#pragma once

String stepsBar(int step) {
  String h;

  h += "<div class='steps'>";

  h += "<div class='step-dot ";
  h += step == 1 ? "active" : "done";
  h += "'>1</div>";

  h += "<div class='step-line ";
  h += step >= 2 ? "done" : "";
  h += "'></div>";

  h += "<div class='step-dot ";
  if (step == 2) h += "active";
  else if (step > 2) h += "done";
  h += "'>2</div>";

  h += "<div class='step-line ";
  h += step >= 3 ? "done" : "";
  h += "'></div>";

  h += "<div class='step-dot ";
  h += step >= 3 ? "done" : "";
  h += "'>3</div>";

  h += "</div>";

  return h;
}
