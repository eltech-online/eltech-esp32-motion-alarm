#pragma once

// The alarm's web page, served as-is by handleRoot(). Its JavaScript (bottom of
// this file) asks the board for /state once a second and updates the page.
// The buttons send a POST to /arm, /relay or /settings. The colours and layout
// come from /style.css (see eltech_wifi.h).
const char PAGE_TEMPLATE[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ElTech-Online Motion Alarm</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <h1>ElTech-Online</h1>
  <div class="sub">ESP32 Motion Alarm &mdash; updated every second</div>
  <div class="cards">
    <div class="card"><div class="label">Alarm</div><div class="value" id="state">--</div>
      <div class="state"><span class="badge" id="left"></span></div></div>
    <div class="card"><div class="label">Sensor</div><div class="value" id="motion">--</div></div>
    <div class="card"><div class="label">Alarms</div><div class="value" id="alarms">--</div></div>
  </div>
  <div class="panel">
    <div class="head"><h2>Control</h2><span class="badge" id="relaystate">--</span></div>
    <div class="buttons">
      <button onclick="post('/arm', 'on=1')">Arm</button>
      <button class="quiet" onclick="post('/arm', 'on=0')">Disarm</button>
      <button class="quiet" id="relaybtn" onclick="post('/relay', 'on=' + (state.relay ? 0 : 1))">Relay on / off</button>
    </div>
  </div>
  <div class="panel">
    <div class="head"><h2>Settings</h2></div>
    <div class="row"><span class="name">Alarm length (seconds)</span>
      <input type="number" id="alarm_s" min="2" max="600" onchange="post('/settings', 'alarm_s=' + this.value)"></div>
    <div class="row"><span class="name">Buzzer sounds on alarm</span>
      <button class="quiet" id="buzzer" onclick="post('/settings', 'buzzer=' + (state.buzzer ? 0 : 1))">--</button></div>
    <div class="row"><span class="name">Relay switches on alarm</span>
      <button class="quiet" id="relayalarm" onclick="post('/settings', 'relay=' + (state.relay_on_alarm ? 0 : 1))">--</button></div>
  </div>
  <div class="panel">
    <div class="head"><h2>Event log</h2></div>
    <div id="log"></div>
    <div class="msg">Kept while the board is powered. The newest event is at the top.</div>
  </div>
  <div class="footer">ESP32 + PIR sensor + relay + buzzer &middot; low-voltage loads only &middot; <a href="https://github.com/eltech-online/eltech-esp32-motion-alarm" target="_blank">github.com/eltech-online/eltech-esp32-motion-alarm</a></div>
  <script>
    const el = (id) => document.getElementById(id);
    let state = {};
    // Turns a number of seconds into "just now", "45 s ago", "3 min ago"...
    function ago(s) {
      if (s < 5) return 'just now';
      if (s < 60) return s + ' s ago';
      if (s < 3600) return Math.floor(s / 60) + ' min ago';
      return Math.floor(s / 3600) + ' h ago';
    }
    // Puts the board's answer on the page.
    function show(d) {
      state = d;
      el('state').textContent = d.state;
      el('state').style.color = d.state === 'ALARM' ? '#fca5a5' : '';
      el('left').textContent = d.left > 0 ? d.left + ' s' : '';
      el('motion').textContent = d.motion ? 'MOTION' : 'quiet';
      el('alarms').textContent = d.alarms;
      el('relaystate').textContent = d.relay ? 'RELAY ON' : 'RELAY OFF';
      el('relaystate').className = 'badge ' + (d.relay ? 'warn' : 'ok');
      if (document.activeElement !== el('alarm_s')) el('alarm_s').value = d.alarm_s;
      el('buzzer').textContent = d.buzzer ? 'Yes' : 'No';
      el('relayalarm').textContent = d.relay_on_alarm ? 'Yes' : 'No';
      const log = el('log');
      log.textContent = '';
      for (const [seconds, text] of d.log) {
        const row = document.createElement('div');
        row.className = 'row';
        const name = document.createElement('span');
        name.className = 'name';
        name.textContent = text;
        const when = document.createElement('span');
        when.className = 'detail';
        when.textContent = ago(seconds);
        row.append(name, when);
        log.append(row);
      }
    }
    async function post(path, query) {
      try {
        const r = await fetch(path + '?' + query, { method: 'POST' });
        show(await r.json());
      } catch (e) { /* board busy or out of range: the next refresh catches up */ }
    }
    async function refresh() {
      try { show(await (await fetch('/state')).json()); } catch (e) {}
    }
    refresh();
    setInterval(refresh, 1000);
  </script>
</body>
</html>
)HTML";
