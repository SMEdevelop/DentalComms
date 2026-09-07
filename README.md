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

2. **Unzip the whole folder**, then **double-click `DentalFlow.exe`** inside it.

   A small window opens saying *"Dental Flow is running"* and lists the web
   addresses it can be reached at, with buttons to copy the staff address or
   open it here.

   **Closing that window no longer turns Dental Flow off.** It tucks itself
   into the notification area (the ⌃ arrow by the clock) and keeps running —
   click the icon to bring it back, or right-click it for *Turn Dental Flow
   off*. That is a change from earlier versions, where closing the black
   console window stopped the whole office's messaging, which is not something
   a window close should ever do.

   While you are there, right-click that icon and tick **Start with
   Windows**. The server computer will then bring Dental Flow up by itself,
   straight into the notification area, whenever it restarts.

   > There is nothing to install first, and no Node.js or anything else to
   > download. Keep the folder together though: the `.dll` files next to
   > `DentalFlow.exe` are part of the program and it will not start without
   > them, so unzip the folder rather than dragging just the `.exe` out of it.
   >
   > If Windows Firewall asks whether to allow it on your network the first
   > time, click **Allow** — staff computers can't reach it otherwise.

3. **On every staff computer**, open a web browser (Chrome, Edge, Safari — any)
   and type in one of the *"Other computers"* addresses shown in that window.
   **Bookmark it** and drag the bookmark to the desktop or bookmarks bar so
   staff can open it with one click.

   The window offers three ways in, and all of them work:

   ```
     On this computer:  http://localhost:3272
     Other computers:   http://RECEPTION-PC:3272
                        http://192.168.1.25:3272
   ```

   > **Bookmark the name, not the number.** Router restarts can change a
   > computer's number address, silently breaking every bookmark in the office
   > at once — and it looks like Dental Flow has died rather than like the
   > address moved. `http://RECEPTION-PC:3272` keeps working when the number
   > changes, which is why it's listed first. Alternatively, ask whoever
   > manages your router to "reserve" the server's IP address.
   >
   > `localhost` only works *on the server computer itself*. It's there so
   > whoever set it up can check it immediately, not for staff machines.

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

  To have that window appear **by itself when the computer starts**, don't use
  this button — see [Set up this computer](#set-up-this-computer-once-per-machine)
  below. The pop-out button can only ever work when a person presses it.

## Set up this computer (once per machine)

Open **🖥️ Set up this computer** in the header, or go straight to
`/setup`. It does two things, and one of them fixes the most common
complaint about Dental Flow.

### Have it open by itself at startup

The page offers a **shortcut to download**. Drop it into the startup folder
(press `Win+R`, type `shell:startup`, drag it in) and Dental Flow opens in its
own window every time that computer starts, with nobody pressing anything.

**Why a shortcut rather than the ⧉ Pop out button.** The pop-out button calls
the browser's `window.open()`, and every browser only honours that inside a
real click — so it *cannot* fire by itself at sign-in, however it is
configured. A computer that restarts comes up with Dental Flow as an ordinary
tab behind everything else, and somebody has to notice and press the button.
That is not a setting anyone got wrong; it is how browsers work.

The downloaded shortcut starts the browser with `--app=`, which makes exactly
the window the pop-out button was trying to make — no tabs, no address bar,
its own taskbar entry — and no pop-up blocker is involved, because no pop-up
is involved.

> The shortcut is built by the server, so it points at the browser installed
> **on the server computer**. Both an Edge and a Chrome version are offered;
> take whichever matches the staff machine. Edge is the default because it is
> on every supported Windows and cannot be removed.

### Or install it as an app

The same page has an **Install** button where the browser supports it. That
gives Dental Flow its own icon and its own window, and putting a copy of that
icon in `shell:startup` has the same effect. Use either route — you do not
need both.

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

**Putting the buttons in the right order.** Each button has ▲ ▼ beside it in
Settings. The order there is the order they appear on every screen, so the
message you send twenty times a day belongs at the top. Reordering is instant
for everyone — nobody needs to reload.

**How alerts announce themselves.** Also in Settings, under *How alerts
announce themselves*:

| | |
|---|---|
| **Play a chime** | Repeats until someone taps *Got it*. Faster and higher for an urgent button. |
| **Flash the screen** | Tints the whole screen a few times as the message arrives — red for urgent, blue otherwise. |
| **Blink the taskbar** | For a station whose Dental Flow window is behind something else: blinks its taskbar button and raises a notification from Windows. |

All three are on to start with, and all three apply to every station. The
flash earns its keep where a tablet has been muted or a surgery is too noisy
to hear the chime; the chime earns its keep when nobody is looking at the
screen. Turn them all off and a message still appears and waits, just
silently.

**Getting the taskbar blink right.** It is aimed at the front desk, where
Dental Flow spends the day behind the practice management software. Two things
make it work properly on such a station:

- Press **⧉ Pop out** once. A pop-out is its own window with its own taskbar
  button, and blinking that button is the whole point. An ordinary tab shares
  a taskbar button with every other tab, labelled by whichever one is on top,
  so there is nothing of its own to blink.
- Say **yes** to the notification permission the browser asks for on the first
  click after this is switched on. That is what puts a real Windows
  notification on screen when the browser is minimised altogether; clicking it
  brings Dental Flow back to the front. Saying no costs only that — the button
  and its icon still blink.

The blinking stops the moment somebody clicks back into the window, on the
grounds that the alert filling the screen has done its job by then, and taps
*Got it* for real to clear it. A message waiting behind that one takes the
blinking over.

The flash deliberately pulses twice a second and stops after three — slow
enough to stay clear of the flash rate associated with photosensitive
seizures — and a station whose device is set to "reduce motion" gets a single
steady tint instead.

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

`"startMinimized": true` makes the server go straight to the notification area
whenever it starts, with no window at all. The **Start with Windows** tick in
the tray menu does the same for the automatic launch only, by passing
`--tray` — so you can have it appear quietly at sign-in and still get a window
when you start it by hand.

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
| An address in the window doesn't work from other computers | Use another one it lists. Dental Flow already hides the addresses it knows are unreachable (see below), but it cannot always tell. |

### Which addresses it offers you

The *"Other computers"* lines are the addresses staff can bookmark. Dental Flow
leaves out the ones that would only waste your time:

- **Virtual adapters.** A PC with Hyper-V, WSL, Docker, VirtualBox or VMware on
  it grows extra network interfaces — typically `172.x.x.x` — that exist only
  inside that computer. Nothing on your office network can reach them.
- **`169.254.x.x` addresses.** Windows invents one of these when an adapter
  asked for an address and nothing answered. It reaches nothing.

It decides by adapter name and by whether the adapter has a router to talk
through. If that filtering would leave *no* addresses at all — an office
running a bare switch with fixed addresses and no router, say — it shows
everything instead, on the grounds that an awkward list beats an empty one.

None of this changes what Dental Flow listens on. It still accepts connections
on every interface when the network profile allows it; this only affects which
addresses it *suggests*.

---

## For whoever maintains this

Dental Flow used to be a Node.js script. It is now a single C++ program with no
runtime to install — that is the only thing that changed. The browser side is
byte-for-byte the same `public/` folder, and the server speaks the identical
API, so nothing on staff computers needed touching and `data/config.json` from
the Node version is read as-is.

Building needs the Microsoft C++ compiler (MSVC). If you don't have it:

```bash
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

That is the compiler only — no Visual Studio IDE. The scripts find it by
themselves through `vswhere`, so there is no need to open a Developer Command
Prompt first:

```bash
build.bat        # produces DentalFlow.exe
run_tests.bat    # 208 checks over every non-networking part
package.bat      # the zip that goes to the office, in dist\
```

As of beta 0.7 the build links the C++ runtime dynamically (`/MD`), so
`DentalFlow.exe` needs `msvcp140.dll`, `vcruntime140.dll` and
`vcruntime140_1.dll` beside it. `package.bat` puts them in the zip.

**Never copy the bare .exe to a front desk machine.** It starts here because
this machine has Visual Studio on it and those DLLs are in `System32`; on a
machine that has never had Visual Studio it dies immediately with a dialog
naming the missing DLL, before it prints anything at all. Always send the zip
`package.bat` makes — unzip it and double-click, still nothing to install.

### Signing

`package.bat` signs nothing unless you point it at a certificate, and says so
when it hasn't. Set one of:

```bash
set DENTALFLOW_SIGN_PFX=C:\path\to\cert.pfx
set DENTALFLOW_SIGN_PASS=...

rem or, for a hardware token or a cloud signing service, a command that
rem takes the file to sign as its only argument:
set DENTALFLOW_SIGN_CMD=powershell -File packaging\sign.ps1
```

The exe is signed before it goes into the zip, and timestamped so the
signature outlives the certificate. A signing command that fails stops
packaging outright rather than quietly shipping an unsigned build.

Worth doing even for one office: unsigned, whoever sets this up meets
SmartScreen's *"Windows protected your PC"* box, and every rebuild starts that
reputation again from zero. `packaging\sign.ps1` drives
[Azure Artifact Signing](https://azure.microsoft.com/en-us/products/artifact-signing)
(formerly Trusted Signing) — roughly $10/month, no hardware token, with an
individual-developer path if you aren't signing as a registered company. Its
header has the setup. Note that signing does **not** silence SmartScreen
immediately; reputation builds with download volume, and
[EV certificates no longer grant it by default](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

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
