/* Dental Comms client */
(() => {
  const $ = id => document.getElementById(id);

  // Before the rename to Dental Comms these were kept under "dentalflow-".
  // Carry them over, so no computer has to be told which room it is again.
  for (const k of ['station', 'tip-multi']) {
    const old = localStorage.getItem('dentalflow-' + k);
    if (old === null) continue;
    if (localStorage.getItem('dentalcomms-' + k) === null)
      localStorage.setItem('dentalcomms-' + k, old);
    localStorage.removeItem('dentalflow-' + k);
  }

  let config = { officeName: 'Dental Comms', stations: [], presets: [] };
  let signals = [];            // all active signals office-wide
  let recent = [];
  let listening = [];          // station ids with a browser open right now
  let myStation = localStorage.getItem('dentalcomms-station') || null;
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
    askToNotify();
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
  // ---------------- screen flash ----------------
  let flashTimer = null;
  function flashScreen(urgent) {
    // Absent when talking to an older server; only an explicit false is off.
    if (config.notifyFlash === false) return;
    const el = $('flash');
    if (!el) return;
    el.style.setProperty('--flash-color', urgent ? 'var(--msg-red)' : 'var(--primary)');
    // Removing the class and forcing a reflow before re-adding it is the only
    // reliable way to replay a CSS animation that may still be running - a
    // second message arriving mid-flash has to restart it, not be swallowed.
    el.classList.remove('on');
    void el.offsetWidth;
    el.classList.add('on');
    clearTimeout(flashTimer);
    flashTimer = setTimeout(() => el.classList.remove('on'), 2000);
  }

  // Both halves of "announce this message", so the repeat timer below cannot
  // drift into chiming without flashing or the other way round.
  function announce(urgent) {
    playChime(urgent);
    flashScreen(urgent);
  }

  function playChime(urgent) {
    // config.notifySound may be absent when talking to an older server, and
    // the chime always rang there, so only an explicit false silences it.
    if (config.notifySound === false) return;
    if (!ensureAudio()) return;
    const t = audioCtx.currentTime;
    if (urgent) { beep(880, t, .18); beep(880, t + .22, .18); beep(1175, t + .44, .3); }
    else { beep(660, t, .25); beep(880, t + .28, .35); }
  }

  // ---------------- taskbar attention ----------------
  // For the station whose Dental Comms window is behind the practice software.
  // The chime and the screen flash both assume somebody is in front of this
  // window; when nobody is, the only place left to shout is the taskbar.
  //
  // A web page cannot flash a taskbar button the way a native program can -
  // Windows has no such API for browsers. What it can do is drive the two
  // things Windows draws on the reader's behalf: the window title, which is
  // the taskbar button's label, and the icon. Blinking those blinks the
  // button. That is why the pop-out button matters here - a pop-out (or an
  // installed shortcut) is its own taskbar button showing its own title,
  // whereas an ordinary tab is labelled by whichever tab is on top.
  //
  // The system notification is the other half, and the half that carries over
  // a fully minimised browser: a real toast from the operating system, which
  // raises this window when clicked.
  let blinkTimer = null;
  let blinkOn = false;
  let attentionSig = null;      // the alert the taskbar is calling about
  let askedToNotify = false;
  const notes = new Map();      // signal id -> its system notification
  const iconCache = {};
  const iconLink = document.querySelector('link[rel="icon"]');
  const baseIcon = iconLink ? iconLink.getAttribute('href') : null;

  function normalTitle() {
    return (myStation ? stationName(myStation) + ' — ' : '') +
           (config.officeName || 'Dental Comms');
  }

  // A dot in the message's own color, drawn rather than shipped so it can
  // match a button whose color the office changed in Settings.
  function alertIcon(color) {
    if (iconCache[color]) return iconCache[color];
    try {
      const c = document.createElement('canvas');
      c.width = c.height = 64;
      const g = c.getContext('2d');
      g.fillStyle = color;
      g.beginPath(); g.arc(32, 32, 31, 0, Math.PI * 2); g.fill();
      g.fillStyle = '#fff';
      g.font = 'bold 46px system-ui, Segoe UI, sans-serif';
      g.textAlign = 'center'; g.textBaseline = 'middle';
      g.fillText('!', 32, 35);
      return (iconCache[color] = c.toDataURL('image/png'));
    } catch {
      return (iconCache[color] = baseIcon);   // canvas blocked: keep the icon
    }
  }

  function paintAttention(on) {
    const sig = attentionSig;
    document.title = on && sig
      ? 'New message: ' + (sig.note || sig.label) + ' — ' + stationName(sig.from)
      : normalTitle();
    if (iconLink && baseIcon)
      iconLink.href = on && sig ? alertIcon(sig.color) : baseIcon;
  }

  // Asked on a real click, because Safari and Firefox refuse the prompt
  // without one, and never a second time - a station that said no keeps the
  // blinking title and icon, which need no permission from anybody.
  function askToNotify() {
    if (askedToNotify || config.notifyTaskbar === false) return;
    // Settings has not arrived yet, and an office that switched this off
    // should never see the prompt at all. It will be here within the moment
    // it takes to open the stream, and the next click asks.
    if (!config.stations.length) return;
    if (!('Notification' in window) || Notification.permission !== 'default') return;
    askedToNotify = true;
    try { Notification.requestPermission(); } catch { /* older signature */ }
  }

  function systemNotify(sig) {
    if (!('Notification' in window) || Notification.permission !== 'granted') return;
    if (notes.has(sig.id)) return;      // already announced, still unanswered
    try {
      const n = new Notification(stationName(sig.from) +
                                 (sig.to === 'all' ? ' (to everyone)' : ''), {
        body: sig.note || sig.label,
        tag: 'dentalcomms-' + sig.id,    // a repeat replaces, never stacks
        icon: 'icon-192.png',
        requireInteraction: true,       // stays up until it is dealt with
        // The page's own chime is the office's sound and Settings decides
        // whether it plays at all; a second sound from Windows would answer
        // to nothing on that page.
        silent: true
      });
      n.onclick = () => { window.focus(); n.close(); };
      notes.set(sig.id, n);
    } catch { /* some browsers only allow these from a service worker */ }
  }

  // Safe to call as often as it takes: it starts nothing that is already
  // running and announces nothing that has already been announced. The three
  // ways it stops are the setting going off, the message being answered, and
  // the reader coming back to a window whose screen the alert already fills.
  function updateAttention() {
    const want = config.notifyTaskbar !== false && !!attentionSig &&
                 !document.hasFocus();
    if (want) {
      systemNotify(attentionSig);
      if (!blinkTimer) {
        blinkOn = true;
        paintAttention(true);
        blinkTimer = setInterval(() => paintAttention(blinkOn = !blinkOn), 900);
      }
    } else if (blinkTimer) {
      clearInterval(blinkTimer);
      blinkTimer = null;
      paintAttention(false);
    }
  }

  function callAttention(sig) {
    // Whatever the taskbar was calling about before has been dealt with. Take
    // its toast down rather than leaving a message somebody already answered
    // sitting in the corner of the screen underneath the new one.
    for (const [id, n] of notes) {
      if (id !== sig.id) { try { n.close(); } catch {} notes.delete(id); }
    }
    attentionSig = sig;
    updateAttention();
  }

  function clearAttention() {
    attentionSig = null;
    updateAttention();
    for (const n of notes.values()) { try { n.close(); } catch {} }
    notes.clear();
  }

  // Coming back to the window is as good as tapping Got it as far as the
  // taskbar is concerned: the alert is on screen in front of them now.
  window.addEventListener('focus', updateAttention);
  window.addEventListener('blur', updateAttention);
  document.addEventListener('visibilitychange', updateAttention);

  // ---------------- rendering ----------------
  function stationName(id) {
    if (id === 'all') return 'Everyone';
    const s = config.stations.find(x => x.id === id);
    return s ? s.name : 'Unknown';
  }

  function renderHeader() {
    $('office-name').textContent = config.officeName || 'Dental Comms';
    $('my-station-btn').textContent = myStation ? 'This is: ' + stationName(myStation)
                                                : 'Choose a room';
    // Every message anyone sends redraws this. Writing the title here while
    // the taskbar is blinking would land on whichever half of the blink was
    // showing and stick there, so leave it to paintAttention until it stops.
    if (!blinkTimer) document.title = normalTitle();
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
    everyone.className = 'sme-tile is-everyone' + (allPicked ? ' is-selected' : '');
    everyone.setAttribute('aria-pressed', allPicked);
    everyone.innerHTML = '<span class="sme-check"></span>' + dcIcon('users', 'dc-icon sme-tile-icon') +
      'Everyone<span class="sme-sub"></span>';
    everyone.querySelector('.sme-sub').textContent = away
      ? `All rooms · ${away} with no screen open` : 'All rooms';
    everyone.onclick = () => {
      selected = allPicked ? new Set() : new Set(others.map(s => s.id));
      renderGrid(); renderSendBar();
    };
    grid.appendChild(everyone);

    for (const s of others) {
      const b = document.createElement('button');
      const nobodyThere = !listening.includes(s.id);
      b.className = 'sme-tile' + (selected.has(s.id) ? ' is-selected' : '') +
        (nobodyThere ? ' is-away' : '');
      b.setAttribute('aria-pressed', selected.has(s.id));
      const check = document.createElement('span');
      check.className = 'sme-check';
      b.append(check, document.createTextNode(s.name));
      // Say so before the message is sent, rather than leaving someone
      // waiting on a checkmark from a room with nothing switched on.
      if (nobodyThere) {
        const note = document.createElement('span');
        note.className = 'sme-sub';
        note.textContent = 'No screen open';
        b.appendChild(note);
      }
      const active = signals.find(sig => sig.to === s.id);
      if (active) {
        const dot = document.createElement('span');
        dot.className = 'busy-dot';
        dot.style.background = active.color;
        dot.title = 'Waiting: ' + active.label;
        const words = document.createElement('span');
        words.className = 'sr-only';
        words.textContent = ', waiting: ' + active.label;
        b.append(dot, words);
      }
      b.onclick = () => toggleSelect(s.id);
      grid.appendChild(b);
    }
  }

  // Shown only while at least one room is picked, as in 0.9.4. Clear, or
  // sending, takes it away again.
  function renderSendBar() {
    const bar = $('send-bar');
    if (!selected.size) { bar.hidden = true; return; }
    const others = otherStations();
    const label = selected.size === others.length && others.length > 1 ? 'everyone'
      : selected.size <= 2 ? listNames([...selected])
      : `${selected.size} rooms`;
    const btn = $('send-bar-btn');
    btn.innerHTML = dcIcon('send');
    btn.append(`Send message to ${label}`);
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
      li.className = 'hint';
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

  // ---------------- first visit: room, sound, startup ----------------
  // A new computer is walked through three things, one tap each: which room
  // it is in, whether it can hear the chime, and - on a Windows desktop -
  // whether Dental Comms should open by itself every morning. The tap that
  // picks the room is also what lets the browser play sound, so the sound
  // check can happen there and then instead of at the first missed alert.
  //
  // A computer that already had a room before this existed has done all of
  // this in practice, so it is never sent through it after an update.
  const ONBOARDED_KEY = 'dentalcomms-onboarded';
  function isOnboarded() {
    try { return !!localStorage.getItem(ONBOARDED_KEY); } catch { return true; }
  }
  function setOnboarded() {
    try { localStorage.setItem(ONBOARDED_KEY, '1'); } catch { /* private window */ }
  }
  if (myStation && !isOnboarded()) setOnboarded();

  function setSheet(step, title, lede, hint) {
    $('setup-step').textContent = step || '';
    $('setup-step').hidden = !step;
    $('setup-title').textContent = title;
    $('setup-lede').textContent = lede || '';
    $('setup-lede').hidden = !lede;
    $('setup-hint').textContent = hint || '';
    $('setup-hint').hidden = !hint;
    $('setup-grid').innerHTML = '';
    $('setup-actions').innerHTML = '';
  }

  // Each action is { text, kind: 'primary' | 'quiet', onClick }. The primary
  // goes last, which puts it on the right, or at the bottom when narrow.
  function setActions(list) {
    const box = $('setup-actions');
    box.innerHTML = '';
    const sorted = list.filter(Boolean)
      .sort((a, b) => (a.kind === 'primary') - (b.kind === 'primary'));
    for (const a of sorted) {
      const b = document.createElement('button');
      b.className = 'sme-btn sme-btn--' + a.kind;
      b.textContent = a.text;
      b.onclick = a.onClick;
      box.appendChild(b);
    }
  }

  function closeSetup() {
    $('setup').hidden = true;
    $('setup-actions').innerHTML = '';
  }

  function showSetup() {
    const firstTime = !myStation && !isOnboarded();
    const changing = !!myStation;
    if (firstTime)
      setSheet('Step 1 of 3', 'Where is this computer?',
               'Tap the room it\'s in. You only do this once.', '');
    else
      setSheet('', 'Where is this computer?',
               changing ? 'Messages for that room will appear on this computer.'
                        : 'Tap the room it\'s in. It will be remembered.', '');
    const grid = $('setup-grid');
    for (const s of config.stations) {
      const b = document.createElement('button');
      b.className = 'sme-tile' + (s.id === myStation ? ' is-selected' : '');
      b.setAttribute('aria-pressed', s.id === myStation);
      if (s.id === myStation) {
        const check = document.createElement('span');
        check.className = 'sme-check';
        b.appendChild(check);
      }
      b.append(s.name);
      b.onclick = () => {
        myStation = s.id;
        localStorage.setItem('dentalcomms-station', myStation);
        // Reopen the stream so the server knows which station this browser is
        // now sitting at, and everyone else's screens stop calling it empty.
        connect();
        renderAll();
        if (firstTime) soundStep(0);
        else closeSetup();
      };
      grid.appendChild(b);
    }
    if (changing)
      setActions([{ text: 'Cancel', kind: 'quiet', onClick: closeSetup }]);
    $('setup').hidden = false;
  }

  // The office's arrival sound, played straight away rather than through
  // playChime(): this runs inside the tap that unlocked audio, and playChime
  // would bail out while the browser is still switching sound on.
  function testChime() {
    try {
      ensureAudio();
      audioCtx.resume().then(() => {
        const t = audioCtx.currentTime;
        beep(660, t, .25); beep(880, t + .28, .35);
      }).catch(() => {});
    } catch { /* no audio on this device */ }
  }

  function soundStep(tries) {
    // An office that switched the chime off in Settings has nothing to test.
    if (config.notifySound === false) return startupStep();
    setSheet('Step 2 of 3', 'Did you hear the chime?',
      tries === 0
        ? 'That\'s the sound new messages make on this computer.'
        : 'Still nothing? Check that this computer\'s volume is turned up and ' +
          'not muted (the speaker by the clock), then play it again.', '');
    testChime();
    setActions([
      tries > 0 ? { text: 'Skip for now', kind: 'quiet', onClick: startupStep } : null,
      { text: 'Play it again', kind: 'quiet', onClick: () => soundStep(tries + 1) },
      { text: 'I heard it', kind: 'primary', onClick: startupStep }
    ]);
  }

  // The startup shortcut only means anything on a Windows PC with Edge or
  // Chrome, and nothing at all inside a window that is already Dental Comms'
  // own (an installed app, or the window the shortcut itself opens).
  function canOpenAtStartup() {
    const ua = navigator.userAgent;
    if (!/Windows/.test(ua) || /Mobile|Android|iPhone|iPad/.test(ua)) return false;
    if (!/Edg\/|Chrome\//.test(ua)) return false;
    if (window.name === 'DentalCommsPopup') return false;
    try { if (matchMedia('(display-mode: standalone)').matches) return false; } catch {}
    return true;
  }

  function startupStep() {
    if (!canOpenAtStartup()) return doneStep();
    setSheet('Step 3 of 3', 'Open Dental Comms every morning?',
      'This downloads a small setup file. Open it once, and Dental Comms will ' +
      'appear in its own window whenever this computer starts.', '');
    setActions([
      { text: 'Skip for now', kind: 'quiet', onClick: doneStep },
      { text: 'Set up this computer', kind: 'primary', onClick: downloadStartup }
    ]);
  }

  async function downloadStartup() {
    const url = /Edg\//.test(navigator.userAgent) ? '/setup/shortcut' : '/setup/shortcut?chrome';
    // Asked for once first, so a server that has gone away says so here
    // rather than handing over an error page with a .cmd name on it.
    const res = await fetch(url).catch(() => null);
    if (!res || !res.ok) {
      setSheet('Step 3 of 3', 'Set it up later?',
        'The setup file couldn\'t be made just now. You can do this later from ' +
        '"Set up this computer" in Settings.', '');
      setActions([{ text: 'Finish setup', kind: 'primary', onClick: doneStep }]);
      return;
    }
    const a = document.createElement('a');
    a.href = URL.createObjectURL(await res.blob());
    a.download = 'Set up Dental Comms.cmd';
    document.body.appendChild(a);
    a.click();
    a.remove();
    setSheet('Step 3 of 3', 'Open the file you just downloaded',
      'Open "Set up Dental Comms" from your downloads, at the top right of the ' +
      'browser. If Windows asks, click Run. It takes a second.',
      'It only adds a shortcut to your Startup folder. To undo it later, ' +
      'delete that shortcut.');
    setActions([
      { text: 'Download it again', kind: 'quiet', onClick: downloadStartup },
      { text: 'Finish setup', kind: 'primary', onClick: doneStep }
    ]);
  }

  function doneStep() {
    setOnboarded();
    setSheet('', 'You\'re all set ✓', `Messages for ${stationName(myStation)} will appear here.`,
      'To pick a different room later, tap "This is" at the top right.');
    setActions([{ text: 'Start using Dental Comms', kind: 'primary', onClick: closeSetup }]);
  }
  $('my-station-btn').onclick = showSetup;

  // ---------------- compose ----------------
  function openCompose() {
    if (!myStation) { showSetup(); return; }
    if (!selected.size) return;
    $('compose-to').textContent = listNames([...selected]);
    $('compose-note').value = '';
    noteError(false);
    const grid = $('preset-grid');
    grid.innerHTML = '';
    for (const pr of config.presets) {
      const b = document.createElement('button');
      const name = MSG_COLOURS[String(pr.color).toLowerCase()];
      b.className = 'sme-msg' + (name ? ' sme-msg--' + name : '');
      if (!name) b.style.background = pr.color;   // an older server's colour
      b.textContent = pr.label;
      // Never colour alone: urgent says so in words.
      if (pr.urgent) {
        const tag = document.createElement('span');
        tag.className = 'sme-urgent';
        tag.textContent = 'URGENT';
        b.appendChild(tag);
      }
      b.onclick = () => sendSignal(pr.id, '');
      grid.appendChild(b);
    }
    $('compose').hidden = false;
  }
  function closeCompose() { $('compose').hidden = true; }
  $('compose-close').onclick = closeCompose;
  $('compose-cancel').onclick = closeCompose;
  // The seven message colours, by the hex the server stores for each.
  const MSG_COLOURS = {
    '#b4483a': 'red', '#4f7a52': 'green', '#3d6b8e': 'blue', '#9a6a2e': 'ochre',
    '#7a5a7e': 'plum', '#3f7a74': 'spruce', '#6f5a48': 'walnut'
  };

  function noteError(on) {
    $('note-field').classList.toggle('is-error', on);
    $('note-hint').textContent = on
      ? 'Type a note first, or tap one of the messages above.'
      : 'Please don\'t include patient names.';
  }
  $('compose-note').addEventListener('input', () => noteError(false));
  $('compose-send-note').onclick = () => {
    const note = $('compose-note').value.trim();
    if (note) sendSignal(null, note);
    else { noteError(true); $('compose-note').focus(); }
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
      hideNotice();
      showToast(`Sent to ${listNames(to)}. Waiting for them to see it…`, { cancel: true });
    } catch {
      showNotice('Your message wasn\'t sent. Check that the main Dental Comms computer is ' +
                 'on, then try again.');
    }
  }

  // ---------------- toast ----------------
  function showToast(text, opts = {}) {
    clearTimeout(toastTimer);
    $('toast-text').textContent = text;
    $('toast').className = 'sme-toast' + (opts.success ? ' sme-toast--success' : '');
    $('toast-cancel').hidden = !opts.cancel;
    $('toast').hidden = false;
    if (!opts.cancel) toastTimer = setTimeout(() => { $('toast').hidden = true; }, 5000);
  }
  // Problems go in a banner at the top, which stays until it is closed:
  // a toast would disappear before anyone read it.
  function showNotice(text) {
    $('notice-text').textContent = text;
    $('notice').hidden = false;
  }
  function hideNotice() { $('notice').hidden = true; }
  $('notice-close').onclick = hideNotice;

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
      // Stop mid-flash rather than letting it finish: the message has been
      // answered, and a screen still pulsing afterwards reads as another one.
      clearTimeout(flashTimer);
      const fl = $('flash');
      if (fl) fl.classList.remove('on');
      clearAttention();
      $('alert').hidden = true;
      return;
    }
    const sig = queue[0];
    const when = sig.time
      ? new Date(sig.time).toLocaleTimeString([], { hour: 'numeric', minute: '2-digit' }) : '';
    $('alert-from').textContent = 'From ' + stationName(sig.from) +
      (sig.to === 'all' ? ' (to everyone)' : '') + (when ? ' · ' + when : '');
    $('alert-label').textContent = sig.label === 'Message' && sig.note ? '' : sig.label;
    $('alert-note').textContent = sig.note || '';
    $('alert-sheet').style.setProperty('--alert-color', sig.color);
    const more = queue.length - 1;
    $('alert-more').textContent = more === 1 ? '1 more message is waiting behind this one.'
      : more > 1 ? `${more} more messages are waiting behind this one.` : '';
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
      announce(sig.urgent);
      chimeTimer = setInterval(() => announce(sig.urgent), sig.urgent ? 3000 : 6000);
      // The taskbar follows the alert on top, so answering this one hands the
      // blinking over to whatever was queued behind it rather than stopping.
      callAttention(sig);
    }
    // Outside that guard: this also has to notice the window being left, and
    // the setting being switched off from the Settings page mid-alert.
    updateAttention();
  }

  // ---------------- recent panel ----------------
  $('recent-toggle').onclick = () => {
    const list = $('recent-list');
    list.hidden = !list.hidden;
    $('recent-toggle').setAttribute('aria-expanded', !list.hidden);
    $('recent-toggle-text').textContent =
      list.hidden ? 'Show recent activity' : 'Hide recent activity';
  };

  // ---------------- the "pick more than one" tip ----------------
  // Says its piece to a station on its first day and stays out of the way
  // afterwards. Remembered per computer, alongside the station name, because
  // that is what having seen it means here - one screen clearing the tip is
  // not the rest of the office claiming to know it.
  const TIP_KEY = 'dentalcomms-tip-multi';
  if (!localStorage.getItem(TIP_KEY)) $('grid-title').hidden = false;
  $('grid-title-done').onclick = () => {
    localStorage.setItem(TIP_KEY, '1');
    $('grid-title').hidden = true;
  };

  // ---------------- connection state ----------------
  // Says which computer to go and look at, by the name this browser used to
  // reach it - the name staff know it by.
  (() => {
    const host = location.hostname;
    const isLocal = host === 'localhost' || host === '127.0.0.1' || host === '[::1]';
    const isNumber = /^[\d.]+$/.test(host) || host.includes(':');
    const where = isLocal
      ? 'Dental Comms is running on this computer (its icon by the clock)'
      : isNumber ? 'the main Dental Comms computer is turned on'
                 : `${host.toUpperCase()} is turned on`;
    $('offline-text').textContent = 'Lost contact with Dental Comms. Messages won\'t ' +
      `arrive until it's back. If this stays, check that ${where}.`;
  })();
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
            ? `✓ Seen by ${stationName(sig.by)}`
            : '✓ Seen by everyone';
          pendingSent = null;
          showToast(msg, { success: true });
        } else {
          showToast(`✓ Seen by ${stationName(sig.by)}. Waiting on ${pendingSent.total - pendingSent.seen} more…`, { cancel: true });
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
        $('toast').hidden = true;
        showNotice(`${stationName(sig.to)} never saw your message. Nobody tapped Got it ` +
                   'before it timed out.');
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
        showNotice(`${stationName(sig.to)} hasn't answered yet. Nobody has tapped Got it.`);
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
        localStorage.removeItem('dentalcomms-station');
        connect();  // drop this browser's claim on a station that is now gone
      }
      renderAll();
      if (!myStation) showSetup();
    });
    es.onerror = () => markOffline();  // EventSource reconnects on its own
  }

  // ---------------- pop-out window ----------------
  // Opens Dental Comms in a small separate window (no tabs/toolbars) that's
  // easy to resize and keep in a corner. Inside that window, hide the button.
  // The second name is the one a window opened before the rename still has.
  if (window.name === 'DentalCommsPopup' || window.name === 'DentalFlowPopup') {
    $('popout-btn').hidden = true;
  } else {
    $('popout-btn').onclick = () => {
      const w = window.open(location.origin, 'DentalCommsPopup',
        'popup=yes,width=430,height=720,left=80,top=80,resizable=yes');
      if (w) { w.focus(); showToast('Opened in its own window. Keep it in a corner.'); }
      else { showNotice('Your browser blocked the new window. Allow pop-ups for this site, then try again.'); }
    };
  }

  // ---------------- boot ----------------
  // Everything is loaded from the "hello" the stream opens with, so the first
  // load and every later reconnection take exactly the same path.
  connect();
})();
