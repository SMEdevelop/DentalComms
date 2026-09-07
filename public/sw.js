// Dental Flow's service worker exists for exactly one reason: Chrome will not
// offer "Install this site as an app" without one. Edge is more lenient, which
// is why installing worked there and not in Chrome.
//
// It is registered ONLY from /setup, never from the page staff use all day.
// A service worker sits in front of every request the page makes - including
// the EventSource that alerts arrive on, which is the one thing this whole
// program depends on staying open - and it is sticky: once a browser has
// registered one, deleting the file from the server does not reliably get rid
// of it. That is a poor trade to make on the everyday page in exchange for an
// Install button, so the everyday page does without and is left exactly as it
// was. Someone visiting /setup is deliberately configuring that computer, and
// can see whether it worked.
//
// It deliberately caches NOTHING.
//
// That is not laziness, it is the whole point. Dental Flow is a live message
// board whose worst possible failure is looking alive when it is not — an
// offline cache would happily serve yesterday's page to a station whose server
// is switched off, with the rooms drawn and the buttons working and no message
// going anywhere. The red "lost contact" bar in the real app is the honest
// behaviour, and it only appears when the page is actually talking to a
// server. So every request goes straight to the network, and when the network
// is gone the browser shows its own error, which is the truth.
//
// If a future version ever caches, it must exclude the event stream and the
// API, and it must not serve index.html from a cache — the page has to be able
// to tell that it cannot reach home.

self.addEventListener('install', () => {
  // Take over immediately rather than waiting for every tab to close. There is
  // nothing cached to migrate, so there is nothing that can go stale.
  self.skipWaiting();
});

self.addEventListener('activate', (event) => {
  event.waitUntil((async () => {
    // Clear anything a previous version of this file may have stored, so an
    // office that updates never keeps a stale page around.
    const names = await caches.keys();
    await Promise.all(names.map((n) => caches.delete(n)));
    await self.clients.claim();
  })());
});

// A fetch handler has to exist for the browser to consider the app
// installable. This one passes everything through untouched.
self.addEventListener('fetch', () => {});
