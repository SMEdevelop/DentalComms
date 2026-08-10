# Dental Flow 📣

A simple intraoffice messaging system, like BlueNote Communicator. Staff tap a
room, tap a big message button ("Doctor needed", "Patient is ready"), and that
room gets a color-coded alert with a chime that repeats until someone taps
**Got it**. The sender sees a ✓ when the message has been seen.

No accounts, no passwords, nothing to install on staff computers — it runs in
any web browser.

> ### ⚠️ Before you rely on it — please read
>
> Dental Flow is a convenience tool for **routine, non-urgent** messages
> between rooms. It is **not** an emergency or nurse-call system, and not a
> medical device.
>
> Messages can fail to arrive for ordinary reasons — a computer asleep, a
> browser closed, Wi-Fi dropped, the server switched off, the volume muted.
> Dental Flow tries hard to make those failures *visible* (rooms with no screen
> open are marked, unanswered alerts are escalated back to the sender, and a red
> bar appears when a screen loses contact) but it cannot promise delivery.
>
> - **Keep your existing way of calling for help** for anything urgent. Never
>   let Dental Flow be the only way to reach someone.
> - **Don't type patient names or other identifying details** into the note
>   box. Use the room, not the person. Notes appear on screens and in the
>   recent-activity list, and cross the office network unencrypted.
> - Provided **as is, with no warranty of any kind**, and with no liability for
>   any message that is delayed or never seen. See [LICENSE](LICENSE) for the
>   full terms, which you accept by running the program.

---

## One-time setup (2 minutes, done by one person)

1. **Pick one Windows computer to be the "server."** Any office computer that
   stays on during the day works. Everyone's messages flow through it.

2. **Start Dental Flow:** double-click `DentalFlow.exe`.

   A window opens saying *"Dental Flow is running"* and shows two web addresses.
   **Leave that window open** — closing it turns Dental Flow off.

   > There is nothing to install first. Dental Flow is a single program with no
   > other requirements. If Windows Firewall asks whether to allow it on your
   > network the first time, click **Allow** — staff computers can't reach it
   > otherwise.

3. **On every staff computer**, open a web browser (Chrome, Edge, Safari — any)
   and type in the *"Other computers"* address shown in that window, something
   like `http://192.168.1.25:3272`. **Bookmark it** and drag the bookmark to
   the desktop or bookmarks bar so staff can open it with one click.

   > **Tip — use the computer's name instead of numbers.** Router restarts can
   > change a computer's number address, silently breaking bookmarks. If the
   > server computer is named, say, `RECEPTION-PC`, bookmark
   > `http://RECEPTION-PC:3272` instead — the name keeps working even when the
   > number changes. (Find the name under Settings → System → About on the
   > server computer.) Alternatively, ask whoever manages your router to
   > "reserve" the server's IP address.

4. Each computer will ask **"Which station is this computer at?"** — tap the
   right one. That's it; it's remembered from then on.

Only the server computer needs to be Windows. Staff can use anything with a
browser — Windows, Mac, iPad, a phone.

## Everyday use (what staff see)

- **To send:** tap the room you want, then tap a big colored button.
  Or type a short note instead. One tap of **📢 Everyone** reaches all stations.
- **To receive:** an alert fills the screen with a chime. Tap **✓ Got it** —
  the chime stops and the sender sees a checkmark that you saw it.
- **Rooms with nothing switched on** are drawn faded, marked *no screen open*.
  You can still message them, but nobody is going to see it until someone
  opens Dental Flow there.
- **Nobody answering?** If an alert sits unanswered for two minutes, whoever
  sent it is told so — rather than being left assuming help is coming. After
  an hour Dental Flow gives up on it, so an alert sent to an empty room at
  closing time doesn't greet the morning shift.
- **A red bar across the top** means this computer has lost contact with the
  Dental Flow server, and messages may not be arriving. It clears by itself the
  moment the connection is back.
- **Missed something?** Tap **Recent activity** at the bottom of the screen.
- **Keep it in a corner:** tap **⧉ Pop out** in the header to open Dental Flow
  in its own small window — resize it and tuck it beside your other programs.
  The layout shrinks to a single column when the window is narrow. (If nothing
  opens, the browser blocked the pop-up — allow pop-ups for this site.)

## Install it like a real program (recommended)

For the most seamless setup, install Dental Flow as an app on each staff
computer. It gets its own desktop/taskbar icon and always opens in its own
window — staff never have to think about the browser at all:

- **Microsoft Edge:** open the Dental Flow page, then menu (⋯) →
  **Apps** → **Install this site as an app** → name it "Dental Flow" → Install.
- **Google Chrome:** open the Dental Flow page, then menu (⋮) →
  **Cast, save, and share** → **Create shortcut…** (on older Chrome:
  More tools → Create shortcut) → tick **Open as window** → Create.

Then right-click the new Dental Flow icon and **Pin to taskbar** so it's one
click from anywhere. To have it start with the computer, press `Win+R`, type
`shell:startup`, and drag a copy of the icon into that folder.
- If sounds are off, tap anywhere on the page once (browsers require one tap
  before they'll play sound).

## Changing rooms and buttons

Tap **⚙️ Settings** in the top-right corner of any screen to rename the
office, add/remove stations, and edit the quick-message buttons (text, color,
and whether they're "urgent"). Changes appear on everyone's screens instantly.

## Tips

- **Start automatically:** so Dental Flow comes back after a restart, press
  `Win+R`, type `shell:startup`, and put a shortcut to `DentalFlow.exe` in the
  folder that opens.
- **It's private:** everything stays on your office network. Nothing goes to
  the internet.
- **Port number:** Dental Flow talks on port `3272` — an uncommon number chosen
  so it won't clash with other software. If it ever needs to change, create a
  file named `port.txt` in this folder containing just a different number
  (e.g. `8123`), restart Dental Flow, and update the bookmarks to match.
- Settings are saved in `data/config.json`; back that file up if you've done a
  lot of customizing.

## Which networks it will answer on

Dental Flow has no passwords — anyone who can reach it can send alerts and
change settings. That is fine on the office network and a bad idea on hotel
Wi-Fi, so Dental Flow checks what kind of network Windows says it's on and
behaves accordingly.

| Windows calls the network | What Dental Flow does |
|---|---|
| **Domain** (company network) | Serves everyone — staff computers connect normally |
| **Private** (a network you've marked trusted) | Serves everyone |
| **Public** (the default for unfamiliar networks) | **This computer only.** Staff computers can't connect |

The window tells you which one applies every time it starts, so you're never
guessing.

**"Staff can't connect and the window says Public."** Windows has decided your
office network is an unfamiliar one. Tell it otherwise: Settings → Network &
internet → click your connection → set the network profile to **Private**.
Restart Dental Flow and staff computers will reach it.

If several networks are connected at once — office Ethernet plus a phone
hotspot, say — the least trusted one wins, because anything Dental Flow serves
is reachable from the hotspot too.

To change the rules, edit `"networks"` in `data/config.json`:

```json
"networks": { "domain": "office", "private": "office", "public": "local-only" }
```

- `office` — serve everyone on the network
- `local-only` — serve this computer alone
- `off` — refuse to start at all

Settings you make on the ⚙️ Settings page never overwrite this.

Dental Flow also only answers to its own addresses: `localhost`, this
computer's name, and the address a connection actually came in on. That is
what stops a web page someone has open from quietly sending alerts or
rewriting your settings in the background — without it, having no password
would mean anything on the internet could reach in through a staff browser.

If your office reaches Dental Flow by some other name — an alias handed out by
the router, say — list it in `data/config.json` or that name will be refused:

```json
"allowedHosts": ["dentalflow.local"]
```

## Other settings in `data/config.json`

These have no page in ⚙️ Settings; edit the file and restart Dental Flow.
Saving from the Settings page leaves them alone.

| Setting | Default | What it does |
|---|---|---|
| `escalateAfterSeconds` | `120` | How long an alert may go unanswered before the sender is told. `0` turns it off. |
| `expireAfterSeconds` | `3600` | How long before an unanswered alert is given up on entirely. `0` means never. |
| `allowedHosts` | `[]` | Extra names this server may be reached by. |

## If something isn't working

| Problem | Fix |
|---|---|
| Page won't load on staff computers | Is the server window still open? Is the address typed exactly as shown? Both computers on the same office network/Wi-Fi (not "guest" Wi-Fi)? |
| "Dental Flow does not answer to that address" | You reached it by a name it doesn't recognise. Use the address in the Dental Flow window, or add the name to `allowedHosts` (see above). |
| A room always says *no screen open* | Nobody has Dental Flow open there, or that computer picked a different station. Open the page there and check the station name in the top-right. |
| Server window says another program is using the port | Follow the message it prints: put a different number in a `port.txt` file and restart. |
| No sound | Tap anywhere on the page once. Check the computer isn't muted. |
| "Could not send" message | The server computer is off or the window was closed — restart Dental Flow there. |
| Wrong station name on a computer | Tap the station name in the top-right corner and pick again. |
| Staff computers can't reach it, but the server page works | Two possibilities. If the window says *Public*, see "Which networks it will answer on" above. Otherwise Windows Firewall is blocking it — allow `DentalFlow.exe` on private networks. |

---

## For whoever maintains this

Dental Flow used to be a Node.js script. It is now a single C++ program with no
runtime to install — that is the only thing that changed. The browser side is
byte-for-byte the same `public/` folder, and the server speaks the identical
API, so nothing on staff computers needed touching and `data/config.json` from
the Node version is read as-is.

Building needs a C++17 compiler. If you don't have one:

```bash
winget install BrechtSanders.WinLibs.POSIX.UCRT
```

Then:

```bash
build.bat        # produces DentalFlow.exe
run_tests.bat    # 159 checks over every non-networking part
```

The code is laid out as:

```
src/
  main.cpp     port selection, the startup banner, the accept loop
  routes.cpp   the API, static files, and the event stream
  state.cpp    live alerts, the activity log, broadcasting
  config.cpp   settings model, validation, data/config.json
  network.cpp  is this a Domain, Private or Public network?
  http.cpp     sockets, request parsing, Server-Sent Events
  json.cpp     JSON parser/writer
  util.cpp     strings, paths, files
tests/
  selftest.cpp
```

On a Public network the restriction is a `bind()` to `127.0.0.1` rather than
every interface, so it is the operating system refusing the connection, not
Dental Flow choosing what to answer. That also means it needs no firewall
permission at all in that mode.

One connection gets one thread. An office has tens of browsers rather than
thousands, so a thread that may block on a slow client costs nothing and is far
easier to follow than an event loop. Every connection carries a read timeout,
so a browser's speculative pre-connect or a machine that sleeps mid-request
gives its thread back instead of holding one until the next restart.

The browser reloads its whole state from `/api/state` every time the event
stream says `hello` — which is on the first connection and on every automatic
reconnection after it. That is what stops a station that slept, or lost Wi-Fi
for a moment, from silently missing everything that happened while it was gone.
For the same reason the activity log is broadcast by the server rather than
each screen composing its own: one send to five rooms is one line everywhere.

`/api/events` takes a `?station=` parameter, and the set of stations with a
stream open is what drives the *no screen open* marker. A room with no browser
attached is a room where an alert will not be seen, and saying so before the
message is sent is worth more than any amount of delivery reporting after it.

Note for anyone working on the browser side: staff reach Dental Flow over plain
`http://`, which is **not** a secure context. `crypto.randomUUID`,
`crypto.subtle`, service workers and the Notification API are all unavailable
there — they work on the server computer's own `localhost` and nowhere else, so
testing only on the server machine will not show it. `crypto.getRandomValues`
is fine, and `public/admin.html` builds its ids from that.

`server.js.reference` is the original Node implementation, kept for comparison.
It predates everything in the two paragraphs above.

---

## License and disclaimer

Copyright © 2026 SMEdevelop. **All rights reserved.**

Dental Flow is proprietary software. It is not open source: no permission is
granted to use, copy, modify, or distribute it except under a separate written
agreement with the copyright holder.

It is provided **as is**, with no warranty of any kind and no liability for any
loss arising from a message that was delayed, misdirected, or never seen. It is
not a medical device and must not be relied on for anything urgent. The full
terms are in [LICENSE](LICENSE), and running the program accepts them.
