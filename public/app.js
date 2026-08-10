/* Dental Flow client */
(() => {
  const $ = id => document.getElementById(id);

  let config = { officeName: 'Dental Flow', stations: [], presets: [] };
  let signals = [];            // all active signals office-wide
  let recent = [];
  let listening = [];          // station ids with a browser open right now
  let myStation = localStorage.getItem('dentalflow-station') || null;
  let selected = new Set();    // station ids picked to receive the message
  let pendingSent = null;      // my last outgoing send: { ids, total, seen }
  let toastTimer = null;
  let chimeTimer = null;
  let chimingFor = null;       // id of the alert the chime belongs to
  let es = null;               // the live event stream
  let offlineTimer = null;

  // ---------------- audio (no files needed) ----------------
  let audioCtx = null;
  function ensureAudio() {
    if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    if (audioCtx.state === 'suspended') {
      $('sound-hint').hidden = false;
      audioCtx.resume();
    } else {
      $('sound-hint').hidden = true;
    }
    return audioCtx.state === 'running';
  }
  document.addEventListener('pointerdown', () => {
    if (audioCtx && audioCtx.state === 'suspended') audioCtx.resume();
    if (audioCtx && audioCtx.state === 'running') $('sound-hint').hidden = true;
  }, true);

  function beep(freq, when, dur) {
    const o = audioCtx.createOscillator();
    const g = audioCtx.createGain();
    o.type = 'sine'; o.frequency.value = freq;
    g.gain.setValueAtTime(0.0001, when);
    g.gain.exponentialRampToValueAtTime(0.35, when + 0.02);
    g.gain.exponentialRampToValueAtTime(0.0001, when + dur);
    o.connect(g); g.connect(audioCtx.destination);
    o.start(when); o.stop(when + dur + 0.05);
  }
  function playChime(urgent) {
    if (!ensureAudio()) return;
    const t = audioCtx.currentTime;
    if (urgent) { beep(880, t, .18); beep(880, t + .22, .18); beep(1175, t + .44, .3); }
    else { beep(660, t, .25); beep(880, t + .28, .35); }
  }

  // ---------------- rendering ----------------
  function stationName(id) {
    if (id === 'all') return 'Everyone';
    const s = config.stations.find(x => x.id === id);
    return s ? s.name : 'Unknown';
  }

  function renderHeader() {
    $('office-name').textContent = config.officeName || 'Dental Flow';
    $('my-station-btn').textContent = myStation ? stationName(myStation) : 'Choose…';
    document.title = (myStation ? stationName(myStation) + ' — ' : '') + (config.officeName || 'Dental Flow');
  }

  function otherStations() {
    return config.stations.filter(s => s.id !== myStation);
  }

  function listNames(ids) {
    const names = ids.map(stationName);
    if (names.length <= 1) return names.join('');
    return names.slice(0, -1).join(', ') + ' and ' + names[names.length - 1];
  }

  function toggleSelect(id) {
    if (selected.has(id)) selected.delete(id); else selected.add(id);
    renderGrid(); renderSendBar();
  }

  function renderGrid() {
    const grid = $('station-grid');
    grid.innerHTML = '';
    const others = otherStations();
    const allPicked = others.length > 0 && selected.size === others.length;
    const away = others.filter(s => !listening.includes(s.id)).length;

    const everyone = document.createElement('button');
    everyone.className = 'tile everyone' + (allPicked ? ' selected' : '');
    everyone.innerHTML = '<span class="check"></span>📢 Everyone<span class="sub">selects all rooms</span>';
    if (away) everyone.querySelector('.sub').textContent =
      `selects all rooms — ${away} with no screen open`;
    everyone.onclick = () => {
      selected = allPicked ? new Set() : new Set(others.map(s => s.id));
      renderGrid(); renderSendBar();
    };
    grid.appendChild(everyone);

    for (const s of others) {
      const b = document.createElement('button');
      const nobodyThere = !listening.includes(s.id);
      b.className = 'tile' + (selected.has(s.id) ? ' selected' : '') +
        (nobodyThere ? ' away' : '');
      const check = document.createElement('span');
      check.className = 'check';
      b.append(check, document.createTextNode(s.name));
      // Say so before the message is sent, rather than leaving someone
      // waiting on a checkmark from a room with nothing switched on.
      if (nobodyThere) {
        const note = document.createElement('span');
        note.className = 'sub away-note';
        note.textContent = 'no screen open';
        b.appendChild(note);
      }
      const active = signals.find(sig => sig.to === s.id);
      if (active) {
        const dot = document.createElement('span');
        dot.className = 'busy-dot';
        dot.style.background = active.color;
        dot.title = active.label;
        b.appendChild(dot);
      }
      b.onclick = () => toggleSelect(s.id);
      grid.appendChild(b);
    }
  }

  function renderSendBar() {
    const bar = $('send-bar');
    if (!selected.size) { bar.hidden = true; return; }
    const others = otherStations();
    const label = selected.size === others.length && others.length > 1 ? 'everyone'
      : selected.size <= 2 ? listNames([...selected])
      : `${selected.size} rooms`;
    $('send-bar-btn').textContent = `Send a message to ${label} ➤`;
    bar.hidden = false;
  }
  $('send-bar-btn').onclick = () => openCompose();
  $('send-bar-clear').onclick = () => { selected.clear(); renderGrid(); renderSendBar(); };

  function renderRecent() {
    const ul = $('recent-list');
    ul.innerHTML = '';
    if (!recent.length) {
      const li = document.createElement('li');
      li.textContent = 'Nothing yet today.';
      ul.appendChild(li);
      return;
    }
    for (const r of recent.slice(0, 15)) {
      const li = document.createElement('li');
      const span = document.createElement('span');
      span.textContent = r.text;
      const t = document.createElement('span');
      t.className = 't';
      t.textContent = new Date(r.time).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' });
      li.append(span, t);
      ul.appendChild(li);
    }
  }

  function renderAll() {
    selected = new Set([...selected].filter(id => config.stations.some(s => s.id === id) && id !== myStation));
    renderHeader(); renderGrid(); renderSendBar(); renderRecent(); renderAlert();
  }

  // ---------------- choose-station screen ----------------
  function showSetup() {
    const grid = $('setup-grid');
    grid.innerHTML = '';
    for (const s of config.stations) {
      const b = document.createElement('button');
      b.className = 'tile';
      b.textContent = s.name;
      b.onclick = () => {
        myStation = s.id;
        localStorage.setItem('dentalflow-station', myStation);
        $('setup').hidden = true;
        // Reopen the stream so the server knows which station this browser is
        // now sitting at, and everyone else's screens stop calling it empty.
        connect();
        renderAll();
      };
      grid.appendChild(b);
    }
    $('setup').hidden = false;
  }
  $('my-station-btn').onclick = showSetup;

  // ---------------- compose ----------------
  function openCompose() {
    if (!myStation) { showSetup(); return; }
    if (!selected.size) return;
    $('compose-to').textContent = listNames([...selected]);
    $('compose-note').value = '';
    const grid = $('preset-grid');
    grid.innerHTML = '';
    for (const pr of config.presets) {
      const b = document.createElement('button');
      b.className = 'preset';
      b.style.background = pr.color;
      b.textContent = pr.label;
      b.onclick = () => sendSignal(pr.id, '');
      grid.appendChild(b);
    }
    $('compose').hidden = false;
  }
  function closeCompose() { $('compose').hidden = true; }
  $('compose-close').onclick = closeCompose;
  $('compose-cancel').onclick = closeCompose;
  $('compose-send-note').onclick = () => {
    const note = $('compose-note').value.trim();
    if (note) sendSignal(null, note);
  };
  $('compose-note').addEventListener('keydown', e => {
    if (e.key === 'Enter' && e.target.value.trim()) sendSignal(null, e.target.value.trim());
  });

  async function sendSignal(presetId, note) {
    const to = [...selected];
    closeCompose();
    try {
      const r = await fetch('/api/signals', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ from: myStation, to, presetId, note })
      });
      const data = await r.json();
      if (!r.ok) throw new Error(data.error || 'failed');
      pendingSent = { ids: data.signals.map(s => s.id), total: data.signals.length, seen: 0 };
      selected.clear();
      renderGrid(); renderSendBar();
      showToast(`Sent to ${listNames(to)} — waiting for them to see it…`, { cancel: true });
    } catch {
      showToast('⚠️ Could not send — is the Dental Flow server running?', { error: true });
    }
  }

  // ---------------- toast ----------------
  function showToast(text, opts = {}) {
    clearTimeout(toastTimer);
    $('toast-text').textContent = text;
    $('toast').className = opts.success ? 'success' : '';
    $('toast-cancel').hidden = !opts.cancel;
    $('toast').hidden = false;
    if (!opts.cancel) toastTimer = setTimeout(() => { $('toast').hidden = true; }, 5000);
  }
  $('toast-cancel').onclick = async () => {
    if (pendingSent) {
      const ids = pendingSent.ids;
      pendingSent = null;
      await Promise.all(ids.map(id =>
        fetch(`/api/signals/${id}/cancel`, {
          method: 'POST', headers: { 'Content-Type': 'application/json' }
        }).catch(() => {})
      ));
    }
    $('toast').hidden = true;
  };

  // ---------------- incoming alerts ----------------
  function myIncoming() {
    return signals.filter(s => s.from !== myStation && (s.to === myStation || s.to === 'all'));
  }

  function renderAlert() {
    const queue = myIncoming();
    if (!queue.length || !myStation) {
      clearInterval(chimeTimer);
      chimeTimer = null; chimingFor = null;
      $('alert').hidden = true;
      return;
    }
    const sig = queue[0];
    $('alert-from').textContent = stationName(sig.from) + (sig.to === 'all' ? ' (to everyone)' : '');
    $('alert-label').textContent = sig.label === 'Message' && sig.note ? '' : sig.label;
    $('alert-note').textContent = sig.note || '';
    $('alert-sheet').style.setProperty('--alert-color', sig.color);
    $('alert-more').textContent = queue.length > 1 ? `${queue.length - 1} more message(s) waiting behind this one` : '';
    $('alert-ack').onclick = async () => {
      await fetch(`/api/signals/${sig.id}/ack`, {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ by: myStation })
      }).catch(() => {});
      signals = signals.filter(s => s.id !== sig.id);
      renderAll();
    };
    $('alert').hidden = false;
    // Only start the chime when the alert on top actually changes. This runs
    // again on every message anyone in the office sends, and restarting the
    // chime each time would make an unanswered alert re-ring at random.
    if (sig.id !== chimingFor) {
      chimingFor = sig.id;
      clearInterval(chimeTimer);
      playChime(sig.urgent);
      chimeTimer = setInterval(() => playChime(sig.urgent), sig.urgent ? 3000 : 6000);
    }
  }

  // ---------------- recent panel ----------------
  $('recent-toggle').onclick = () => {
    const list = $('recent-list');
    list.hidden = !list.hidden;
    $('recent-toggle').textContent = list.hidden ? 'Recent activity ▾' : 'Recent activity ▴';
  };

  // ---------------- connection state ----------------
  // A short delay keeps a one-second blip from flashing a scary red bar; a
  // genuine outage stays on screen until the stream is really back.
  function markOffline() {
    if (offlineTimer) return;
    offlineTimer = setTimeout(() => {
      $('offline').hidden = false;
      document.body.classList.add('is-offline');
    }, 3000);
  }
  function markOnline() {
    clearTimeout(offlineTimer);
    offlineTimer = null;
    $('offline').hidden = true;
    document.body.classList.remove('is-offline');
  }

  // ---------------- live events ----------------
  async function loadState() {
    const r = await fetch('/api/state');
    const state = await r.json();
    config = state.config;
    signals = state.signals;
    recent = state.recent;
    listening = state.listening || [];
    if (myStation && !config.stations.some(s => s.id === myStation)) myStation = null;
    renderAll();
    if (!myStation) showSetup();
  }

  function connect() {
    if (es) { es.close(); es = null; }
    const q = myStation ? '?station=' + encodeURIComponent(myStation) : '';
    es = new EventSource('/api/events' + q);

    // Sent on every connection, including each automatic reconnection after a
    // dropped Wi-Fi link or a sleeping laptop. Re-reading the whole state here
    // is what stops a station from silently missing everything that happened
    // while it was away, or showing alerts the rest of the office has cleared.
    es.addEventListener('hello', () => {
      markOnline();
      loadState().catch(() => {});
    });

    es.addEventListener('signal-new', e => {
      const sig = JSON.parse(e.data);
      if (!signals.some(s => s.id === sig.id)) signals.push(sig);
      renderAll();
    });
    es.addEventListener('signal-ack', e => {
      const sig = JSON.parse(e.data);
      signals = signals.filter(s => s.id !== sig.id);
      if (pendingSent && pendingSent.ids.includes(sig.id)) {
        pendingSent.ids = pendingSent.ids.filter(id => id !== sig.id);
        pendingSent.seen++;
        if (pendingSent.seen >= pendingSent.total) {
          const msg = pendingSent.total === 1
            ? `✓ ${stationName(sig.by)} saw your message`
            : '✓ Everyone saw your message';
          pendingSent = null;
          showToast(msg, { success: true });
        } else {
          showToast(`✓ ${stationName(sig.by)} saw it — waiting on ${pendingSent.total - pendingSent.seen} more…`, { cancel: true });
        }
      }
      renderAll();
    });
    es.addEventListener('signal-cancel', e => {
      const sig = JSON.parse(e.data);
      signals = signals.filter(s => s.id !== sig.id);
      renderAll();
    });
    // Nobody ever answered and the server has given up on it.
    es.addEventListener('signal-expired', e => {
      const sig = JSON.parse(e.data);
      signals = signals.filter(s => s.id !== sig.id);
      if (pendingSent && pendingSent.ids.includes(sig.id)) {
        pendingSent.ids = pendingSent.ids.filter(id => id !== sig.id);
        if (!pendingSent.ids.length) pendingSent = null;
        showToast(`⚠️ ${stationName(sig.to)} never saw your message.`, {});
      }
      renderAll();
    });
    // Still sitting there unanswered: tell whoever sent it, once, rather than
    // leaving them to assume a room they messaged is on its way.
    es.addEventListener('signal-stale', e => {
      const sig = JSON.parse(e.data);
      const mine = signals.find(s => s.id === sig.id);
      if (mine) mine.stale = true;
      if (pendingSent && pendingSent.ids.includes(sig.id)) {
        showToast(`⚠️ ${stationName(sig.to)} still hasn't answered — nobody has tapped Got it.`,
                  { cancel: true });
        playChime(false);
      }
    });
    // The activity log is the server's, not one each screen invents, so a send
    // to five rooms reads as the one line everybody else sees.
    es.addEventListener('log', e => {
      recent.unshift(JSON.parse(e.data));
      if (recent.length > 50) recent.length = 50;
      renderRecent();
    });
    es.addEventListener('presence', e => {
      listening = (JSON.parse(e.data).listening) || [];
      renderGrid();
    });
    es.addEventListener('config', e => {
      config = JSON.parse(e.data);
      if (myStation && !config.stations.some(s => s.id === myStation)) {
        myStation = null;
        localStorage.removeItem('dentalflow-station');
        connect();  // drop this browser's claim on a station that is now gone
      }
      renderAll();
      if (!myStation) showSetup();
    });
    es.onerror = () => markOffline();  // EventSource reconnects on its own
  }

  // ---------------- pop-out window ----------------
  // Opens Dental Flow in a small separate window (no tabs/toolbars) that's
  // easy to resize and keep in a corner. Inside that window, hide the button.
  if (window.name === 'DentalFlowPopup') {
    $('popout-btn').hidden = true;
  } else {
    $('popout-btn').onclick = () => {
      const w = window.open(location.origin, 'DentalFlowPopup',
        'popup=yes,width=430,height=720,left=80,top=80,resizable=yes');
      if (w) { w.focus(); showToast('Dental Flow opened in its own little window — you can resize it and keep it in a corner.'); }
      else { showToast('⚠️ Your browser blocked the pop-up — allow pop-ups for this site and try again.'); }
    };
  }

  // ---------------- boot ----------------
  // Everything is loaded from the "hello" the stream opens with, so the first
  // load and every later reconnection take exactly the same path.
  connect();
})();
