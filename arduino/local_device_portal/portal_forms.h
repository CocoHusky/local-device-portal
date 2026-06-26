#pragma once

String passwordToggleScript() {
  return R"HTML(
<script>
function togglePass(){
  var p = document.getElementById('pass');
  var b = document.getElementById('eyeBtn');
  if(!p || !b) return;

  var show = p.type === 'password';
  p.type = show ? 'text' : 'password';

  if(show) b.classList.add('showing');
  else b.classList.remove('showing');
}
</script>
)HTML";
}

String eyeButton() {
  return R"HTML(
<button id="eyeBtn" class="eye-btn" type="button" onclick="togglePass()" aria-label="Show or hide password">
  <svg class="eye-open" viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <path d="M2 12s3.5-6 10-6 10 6 10 6-3.5 6-10 6S2 12 2 12z"/>
    <circle cx="12" cy="12" r="3"/>
  </svg>
  <svg class="eye-off" viewBox="0 0 24 24" fill="none" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">
    <path d="M3 3l18 18"/>
    <path d="M10.7 5.2A10.7 10.7 0 0 1 12 5c6.5 0 10 7 10 7a17.4 17.4 0 0 1-3.1 4.2"/>
    <path d="M6.6 6.6C3.6 8.6 2 12 2 12s3.5 7 10 7a10.8 10.8 0 0 0 4.4-.9"/>
    <path d="M9.9 9.9A3 3 0 0 0 14.1 14.1"/>
  </svg>
</button>
)HTML";
}
