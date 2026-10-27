# Dental Comms 📣

> **Formerly Dental Flow.** To upgrade, turn Dental Flow off and run the
> installer, `DentalComms-Setup-<version>.exe`. It copies your rooms and
> buttons (`data\config.json`) over from the folder Dental Flow was starting
> from, moves "Start with Windows" to the new copy, and tells you on its last
> page which old folder you can now delete. Or stay portable: copy the new
> files into your existing folder, delete `DentalFlow.exe`, and start
> `DentalComms.exe`. Either way staff computers keep the room they were set
> to. Windows Firewall sees a new program, so if it asks, click **Allow**
> again.

A simple intraoffice messaging system, like BlueNote Communicator. Staff tap a
room, tap a big message button ("Doctor needed", "Patient is ready"), and that
room gets a color-coded alert with a chime that repeats until someone taps
**Got it**. The sender sees a ✓ when the message has been seen.

No accounts, no passwords, nothing to install on staff computers — it runs in
any web browser.

> ### ⚠️ Before you rely on it — please read
>
> Dental Comms is a convenience tool for **routine, non-urgent** messages
> between rooms. It is **not** an emergency or nurse-call system, and not a
> medical device.
>
> Messages can fail to arrive for ordinary reasons — a computer asleep, a
> browser closed, Wi-Fi dropped, the server switched off, the volume muted.
> Dental Comms tries hard to make those failures *visible* (rooms with no screen
> open are marked, unanswered alerts are escalated back to the sender, and a red
> bar appears when a screen loses contact) but it cannot promise delivery.
>
> - **Keep your existing way of calling for help** for anything urgent. Never
>   let Dental Comms be the only way to reach someone.
> - **Don't type patient names or other identifying details** into the note
>   box. Use the room, not the person. Notes appear on screens and in the
>   recent-activity list, and cross the office network unencrypted.
> - Provided **as is, with no warranty of any kind**, and with no liability for
>   any message that is delayed or never seen. See [LICENSE](LICENSE) for the
>   full terms.

---

## One-time setup (2 minutes, done by one person)

1. **Pick one Windows computer to be the "server."** Any office computer that
   stays on during the day works. Everyone's messages flow through it.

2. **Install it** on that computer. From the
   [releases page](https://github.com/SMEdevelop/DentalComms/releases), take
   one of:

   - **`DentalComms-Setup-<version>.exe`** (recommended): run it and click
     **Install**. That's the only page. It needs no administrator password,
     adds Dental Comms to the Start menu, sets it to start with Windows, and
     can be removed later from *Settings → Apps* like any other program. It
     installs to `%LOCALAPPDATA%\Programs\Dental Comms`.
   - **`DentalComms-<version>-windows-x64.zip`** (portable): **unzip the whole
     folder** anywhere you can write to, then **double-click
     `DentalComms.exe`** inside it. Nothing is installed; delete the folder to
     remove it.

   Both are the same program. If you started with the zip, running the
   installer later moves you over: it copies your rooms and buttons from the
   folder your old copy was starting from.

   If Windows says **"Windows protected your PC"**, click **More info → Run
   anyway**. Releases are not code-signed yet (see
   [Code signing policy](#code-signing-policy)), so Windows doesn't know the
   publisher.

   The first time, a small window walks you through three screens:

   1. **Welcome.** It says what Dental Comms is for and warns you that
      Windows may ask whether Dental Comms can use your network. Click
      **Allow** when it does.
   2. **Checking.** A checklist fills itself in: Dental Comms is running,
      settings can be saved, the network is your office network, Windows
      Firewall will let other computers in, and it starts with Windows.
      Anything that needs you is marked **!** with a plain explanation, and
      usually a button that fixes it (see
      [What the window checks](#what-the-window-checks)).
   3. **Ready.** The address to give every other computer, with buttons to
      copy it, print a setup card, or open it here.

   After that, the same window is a status page: the address, and the same
   checklist kept up to date while Dental Comms runs.

   **Closing that window no longer turns Dental Comms off.** It tucks itself
   into the notification area (the ⌃ arrow by the clock) and keeps running —
   click the icon to bring it back, or right-click it for *Turn Dental Comms
   off*. That is a change from earlier versions, where closing the black
   console window stopped the whole office's messaging, which is not something
   a window close should ever do.

   **Start with Windows** is switched on for you, by the installer or (with
   the zip) by the first start. The server computer brings Dental Comms up by
   itself, straight into the notification area, whenever it restarts. To turn
   that off, right-click the icon and untick it.

   Opening Dental Comms again while it is already running just brings its
   window back. Only one copy runs on a computer.

   > There is nothing else to install first, and no Node.js or anything else
   > to download. With the zip, keep the folder together: the `.dll` files
   > next to `DentalComms.exe` are part of the program and it will not start
   > without them, so unzip the folder rather than dragging just the `.exe`
   > out of it.
   >
   > If Windows Firewall asks whether to allow it on your network the first
   > time, click **Allow**, or staff computers can't reach it. If someone
   > clicked Cancel, the window says so and offers **Let them in**, which
   > fixes it after a Windows administrator prompt.

3. **On every staff computer**, open a web browser (Chrome, Edge, Safari — any)
   and type in the address the window shows, for example
   `http://RECEPTION-PC:3272`. **Bookmark it.**

   **Print a setup card** (a button in the window, or `/card`) gives you one
   page with the address, a QR code that tablets and phones can scan with
   their camera, and these steps. Leave one by each computer.

   The number addresses (`http://192.168.1.25:3272`) work too. The card lists
   them as a fallback.

   > **Bookmark the name, not the number.** Router restarts can change a
   > computer's number address, silently breaking every bookmark in the office
   > at once — and it looks like Dental Comms has died rather than like the
   > address moved. `http://RECEPTION-PC:3272` keeps working when the number
   > changes, which is why it's listed first. Alternatively, ask whoever
   > manages your router to "reserve" the server's IP address.
   >
   > `localhost` only works *on the server computer itself*. It's there so
   > whoever set it up can check it immediately, not for staff machines.

4. The first time, each computer asks three quick things:

   1. **Which room is this computer in?** Tap it. That tap also lets the
      browser play sound.
   2. **Did you hear a chime?** If not, it says to check the volume and plays
      it again.
   3. **Open Dental Comms by itself every morning?** (Windows computers with
      Edge or Chrome only.) **Yes** downloads a small setup file; open it once
      and Dental Comms opens in its own window at every sign-in. See
      [Set up this computer](#set-up-this-computer-once-per-machine).

   That's it; it's remembered from then on. Computers that were already set
   up before this version skip straight to their messages.

Only the server computer needs to be Windows. Staff can use anything with a
browser — Windows, Mac, iPad, a phone.

## What the window checks

The server window keeps a checklist and rechecks it every few seconds. Green
means fine, blue is worth knowing, and **!** needs someone.

| Row | What it means | What you can do |
|---|---|---|
| Dental Comms is running | It is listening for the other computers. | — |
| Using connection number 3273 | The usual number was taken on the first start, so it picked the next free one. Only ever happens before setup is finished, so no bookmarks break. | Nothing. |
| Your settings were repaired / reset | `data\config.json` was damaged. It was set aside as `config.damaged-<date>.json` and replaced by the last good copy (`config.backup.json`) or, failing that, the standard rooms. | Nothing, unless you want something back from the damaged file. |
| Dental Comms can't save its settings | It is in a folder Windows protects. | Use the installer instead. |
| Other computers can't connect yet | Windows calls this network **Public**. | **Yes, this is our office network** marks it Private (administrator prompt). Dental Comms switches over without a restart. |
| Windows Firewall is keeping other computers out | Someone clicked Cancel on Windows' "allow access" box, which leaves a blocking rule behind. | **Let them in** switches off the blocking rule and adds an allow rule for office networks (administrator prompt). |
| Your office's IT settings may be keeping other computers out | Firewall rules come from the domain. | Ask IT to allow Dental Comms (TCP, the port shown). |
| 3 staff screens are connected / Reached from another computer | Proof that staff computers get through. Once this shows, the firewall row is no longer needed. | — |
| Doesn't start by itself yet | It won't come back after a restart. | **Start it with Windows**. |

If the network changes while Dental Comms is running (a laptop server moved
to another network, say), it follows: it closes to other computers on a
Public network and opens up again on the office one, with no restart. When
something goes wrong while the window is hidden, a notification by the clock
says so once; clicking it opens the window.

## Everyday use (what staff see)

- **To send:** tap the room you want, then tap a big colored button.
  Or type a short note instead. One tap of **📢 Everyone** reaches all stations.
- **To receive:** an alert fills the screen with a chime. Tap **✓ Got it** —
  the chime stops and the sender sees a checkmark that you saw it.
- **Rooms with nothing switched on** are drawn faded, marked *no screen open*.
  You can still message them, but nobody is going to see it until someone
  opens Dental Comms there.
- **Nobody answering?** If an alert sits unanswered for two minutes, whoever
  sent it is told so — rather than being left assuming help is coming. After
  an hour Dental Comms gives up on it, so an alert sent to an empty room at
  closing time doesn't greet the morning shift.
- **A red bar across the top** means this computer has lost contact with the
  Dental Comms server, and messages may not be arriving. It clears by itself the
  moment the connection is back.
- **Missed something?** Tap **Recent activity** at the bottom of the screen.
- **Keep it in a corner:** tap **⧉ Pop out** in the header to open Dental Comms
  in its own small window — resize it and tuck it beside your other programs.
  The layout shrinks to a single column when the window is narrow. (If nothing
  opens, the browser blocked the pop-up — allow pop-ups for this site.)

  To have that window appear **by itself when the computer starts**, don't use
  this button — see [Set up this computer](#set-up-this-computer-once-per-machine)
  below. The pop-out button can only ever work when a person presses it.

## Set up this computer (once per machine)

Open **🖥️ Set up this computer** in the header, or go straight to
`/setup`. It does two things, and one of them fixes the most common
complaint about Dental Comms.

### Have it open by itself at startup

The page offers a **shortcut to download**. Drop it into the startup folder
(press `Win+R`, type `shell:startup`, drag it in) and Dental Comms opens in its
own window every time that computer starts, with nobody pressing anything.

**Why a shortcut rather than the ⧉ Pop out button.** The pop-out button calls
the browser's `window.open()`, and every browser only honours that inside a
real click — so it *cannot* fire by itself at sign-in, however it is
configured. A computer that restarts comes up with Dental Comms as an ordinary
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
gives Dental Comms its own icon and its own window, and putting a copy of that
icon in `shell:startup` has the same effect. Use either route — you do not
need both.

## Install it like a real program (recommended)

For the most seamless setup, install Dental Comms as an app on each staff
computer. It gets its own desktop/taskbar icon and always opens in its own
window — staff never have to think about the browser at all:

- **Microsoft Edge:** open the Dental Comms page, then menu (⋯) →
  **Apps** → **Install this site as an app** → name it "Dental Comms" → Install.
- **Google Chrome:** open the Dental Comms page, then menu (⋮) →
  **Cast, save, and share** → **Create shortcut…** (on older Chrome:
  More tools → Create shortcut) → tick **Open as window** → Create.

Then right-click the new Dental Comms icon and **Pin to taskbar** so it's one
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
| **Blink the taskbar** | For a station whose Dental Comms window is behind something else: blinks its taskbar button and raises a notification from Windows. |

All three are on to start with, and all three apply to every station. The
flash earns its keep where a tablet has been muted or a surgery is too noisy
to hear the chime; the chime earns its keep when nobody is looking at the
screen. Turn them all off and a message still appears and waits, just
silently.

**Getting the taskbar blink right.** It is aimed at the front desk, where
Dental Comms spends the day behind the practice management software. Two things
make it work properly on such a station:

- Press **⧉ Pop out** once. A pop-out is its own window with its own taskbar
  button, and blinking that button is the whole point. An ordinary tab shares
  a taskbar button with every other tab, labelled by whichever one is on top,
  so there is nothing of its own to blink.
- Say **yes** to the notification permission the browser asks for on the first
  click after this is switched on. That is what puts a real Windows
  notification on screen when the browser is minimised altogether; clicking it
  brings Dental Comms back to the front. Saying no costs only that — the button
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

- **Start automatically:** so Dental Comms comes back after a restart,
  right-click its icon by the clock and tick **Start with Windows**.
- **It's private:** everything stays on your office network. Nothing goes to
  the internet.
- **The Dental Comms folder** is where `DentalComms.exe` is:
  `%LOCALAPPDATA%\Programs\Dental Comms` if you used the installer (paste that
  into File Explorer's address bar), or wherever you unzipped it.
- **Port number:** Dental Comms talks on port `3272`, an uncommon number
  chosen so it won't clash with other software. If that is taken the very
  first time, it picks the next free one by itself and remembers it (as
  `"port"` in `data\config.json`). After setup it never moves on its own,
  because every bookmark depends on it: if another program takes it, Dental
  Comms says which program and offers **Try again** or **Use a different
  number**. A `port.txt` file in the Dental Comms folder still overrides all
  of this, as before.
- Settings are saved in `data\config.json` in the Dental Comms folder, and
  every save also refreshes `data\config.backup.json`, which is what a
  damaged file is repaired from. Updating keeps both, and uninstalling asks
  before deleting the `data` folder.
- **Updating:** run the new installer. If Dental Comms is running, the
  installer says it will turn it off for about a minute, does so, and starts
  it again afterwards. Silent installs (winget) do the same without asking.
  With the zip, turn it off from the icon by the clock and copy the new files
  over the old ones.

## Which networks it will answer on

Dental Comms has no passwords — anyone who can reach it can send alerts and
change settings. That is fine on the office network and a bad idea on hotel
Wi-Fi, so Dental Comms checks what kind of network Windows says it's on and
behaves accordingly.

| Windows calls the network | What Dental Comms does |
|---|---|
| **Domain** (company network) | Serves everyone — staff computers connect normally |
| **Private** (a network you've marked trusted) | Serves everyone |
| **Public** (the default for unfamiliar networks) | **This computer only.** Staff computers can't connect |

The window tells you which one applies every time it starts, so you're never
guessing.

**"Staff can't connect and the window says Public."** Windows has decided your
office network is an unfamiliar one. Click **Yes, this is our office network**
in the Dental Comms window (it needs an administrator's OK), or do it by hand:
Settings → Network & internet → click your connection → set the network
profile to **Private**. Either way, Dental Comms notices within a few seconds;
no restart needed.

If several networks are connected at once — office Ethernet plus a phone
hotspot, say — the least trusted one wins, because anything Dental Comms serves
is reachable from the hotspot too.

To change the rules, edit `"networks"` in `data/config.json`:

```json
"networks": { "domain": "office", "private": "office", "public": "local-only" }
```

- `office` — serve everyone on the network
- `local-only` — serve this computer alone
- `off` — serve nobody, not even this computer. Dental Comms waits by the
  clock and starts serving by itself once it is on a network it may use.

Settings you make on the ⚙️ Settings page never overwrite this.

Dental Comms also only answers to its own addresses: `localhost`, this
computer's name, and the address a connection actually came in on. That is
what stops a web page someone has open from quietly sending alerts or
rewriting your settings in the background — without it, having no password
would mean anything on the internet could reach in through a staff browser.

If your office reaches Dental Comms by some other name — an alias handed out by
the router, say — list it in `data/config.json` or that name will be refused:

```json
"allowedHosts": ["dentalcomms.local"]
```

## Other settings in `data/config.json`

`"startMinimized": true` makes the server go straight to the notification area
whenever it starts, with no window at all. The **Start with Windows** tick in
the tray menu does the same for the automatic launch only, by passing
`--tray` — so you can have it appear quietly at sign-in and still get a window
when you start it by hand.

These have no page in ⚙️ Settings; edit the file and restart Dental Comms.
Saving from the Settings page leaves them alone.

| Setting | Default | What it does |
|---|---|---|
| `escalateAfterSeconds` | `120` | How long an alert may go unanswered before the sender is told. `0` turns it off. |
| `expireAfterSeconds` | `3600` | How long before an unanswered alert is given up on entirely. `0` means never. |
| `allowedHosts` | `[]` | Extra names this server may be reached by. |
| `port` | picked on first start | The connection number. Written the first time Dental Comms starts; `port.txt` overrides it. |
| `welcomed` | `false` on a new install | Whether the first-run welcome was finished. Set it to `false` to see the welcome again. |

## If something isn't working

| Problem | Fix |
|---|---|
| Page won't load on staff computers | Open the Dental Comms window on the server and look for a row marked **!**. Otherwise: is the address typed exactly as shown? Both computers on the same office network/Wi-Fi (not "guest" Wi-Fi)? |
| "Dental Comms does not answer to that address" | You reached it by a name it doesn't recognise. Use the address in the Dental Comms window, or add the name to `allowedHosts` (see above). |
| A room always says *no screen open* | Nobody has Dental Comms open there, or that computer picked a different station. Open the page there and check the station name in the top-right. |
| "Dental Comms can't start yet" with a program's name | That program is using Dental Comms' connection number. Close it and click **Try again**, or choose **Use a different number** and update the bookmarks to the new address the window shows. |
| No sound | Tap anywhere on the page once. Check the computer isn't muted. |
| "Couldn't send" message, or a red bar at the top | The server computer is off or Dental Comms was turned off there. The red bar names the computer to check. |
| Wrong station name on a computer | Tap the station name in the top-right corner and pick again. |
| Staff computers can't reach it, but the server page works | The window's checklist names the cause: a Public network or a firewall rule, each with a button to fix it. See [What the window checks](#what-the-window-checks). |
| An address in the window doesn't work from other computers | Use another one it lists. Dental Comms already hides the addresses it knows are unreachable (see below), but it cannot always tell. |

### Which addresses it offers you

The *"Other computers"* lines are the addresses staff can bookmark. Dental Comms
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

None of this changes what Dental Comms listens on. It still accepts connections
on every interface when the network profile allows it; this only affects which
addresses it *suggests*.

---

## For whoever maintains this

Dental Comms used to be a Node.js script. It is now a single C++ program with no
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
build.bat        # produces DentalComms.exe
run_tests.bat    # 282 checks over every non-networking part
package.bat      # the installer and the zip that go to the office, in dist\
```

The installer needs [Inno Setup](https://jrsoftware.org/isinfo.php) 6
(`winget install JRSoftware.InnoSetup`); without it `package.bat` still makes
the zip and says it skipped the installer. The script is
[`packaging/installer.iss`](packaging/installer.iss). It installs per-user,
into `%LOCALAPPDATA%\Programs\Dental Comms`, because `data\config.json` and
`port.txt` live beside the exe and have to be writable without running as
administrator — the same layout as the zip, so the two behave identically.

As of beta 0.7 the build links the C++ runtime dynamically (`/MD`), so
`DentalComms.exe` needs `msvcp140.dll`, `vcruntime140.dll` and
`vcruntime140_1.dll` beside it. `package.bat` puts them in the zip and the
installer.

**Never copy the bare .exe to a front desk machine.** It starts here because
this machine has Visual Studio on it and those DLLs are in `System32`; on a
machine that has never had Visual Studio it dies immediately with a dialog
naming the missing DLL, before it prints anything at all. Always send the
installer or the zip `package.bat` makes.

### Signing

**Official releases are built by CI, never on a developer's PC, and are not
signed yet.** Pushing a `v*` tag runs
[`.github/workflows/release.yml`](.github/workflows/release.yml) on a
GitHub-hosted runner. It runs the tests, builds `DentalComms.exe`, packages
the installer and the zip, and attaches everything to a **draft** release for
you to check and publish.

The workflow is ready to sign through SignPath Foundation, which has not
accepted the project yet. Once it does and the SignPath settings the workflow
describes exist, the same run has the exe and the installer's setup engine
signed, packages around them, and has the installer itself signed. Each
signing request waits for an approver to accept it in SignPath, so a release
cannot be signed without someone saying yes.

**Pushing a tag that already has a release replaces that release's files**
with a fresh build (`gh release upload --clobber`), which changes their
checksums and breaks any winget manifest that points at them. Don't move or
re-push a published tag.

For copies you build and run yourself, `build.bat`, `run_tests.bat` and
`package.bat` sign nothing unless you point them at a certificate, and say so
when they haven't. On a PC with Smart App Control on, an unsigned local build
will not start at all. Set one of:

```bash
set DENTALCOMMS_SIGN_PFX=C:\path\to\cert.pfx
set DENTALCOMMS_SIGN_PASS=...

rem or, for a hardware token or a cloud signing service, a command that
rem takes the file to sign as its only argument:
set DENTALCOMMS_SIGN_CMD=powershell -File packaging\sign.ps1
```

`packaging\sign.ps1` drives
[Azure Artifact Signing](https://azure.microsoft.com/en-us/products/artifact-signing);
its header has the setup. A signing command that fails stops the build
outright rather than quietly producing an unsigned exe.

`package.bat --prebuilt` skips the build and packages whatever
`DentalComms.exe` already is. CI uses it to package the exe that has just come
back signed; there is little reason to use it by hand. `--engine-only` and
`--signed-engine` are CI's too: an Inno Setup installer runs a second program,
its setup engine, from `%TEMP%`, and Smart App Control blocks that engine
unless it carries a signature of its own. With signing set up, `package.bat`
signs the engine as well.

Signing does **not** silence SmartScreen immediately; reputation builds with
download volume, and
[EV certificates no longer grant it by default](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/smartscreen-reputation).

### How the code is laid out

The code is laid out as:

```
src/
  main.cpp     startup: port selection, following the network, the accept loop
  gui.cpp      the window: first-run welcome, status checklist, tray icon
  checks.cpp   the checklist's rows and words, built from plain facts
  firewall.cpp reading Windows Firewall's rules, and the elevated fix
  routes.cpp   the API, static files, and the event stream
  state.cpp    live alerts, the activity log, broadcasting
  config.cpp   settings model, validation, data/config.json, backup/repair
  network.cpp  is this a Domain, Private or Public network? (and the fix)
  http.cpp     sockets, request parsing, Server-Sent Events
  json.cpp     JSON parser/writer
  util.cpp     strings, paths, files
tests/
  selftest.cpp
```

`DentalComms.exe` takes a few switches besides `--tray`. `--quit` asks the
running copy to turn off. `--fix-firewall` and `--trust-network` are the two
jobs that need an administrator: the window starts a second copy with one of
them through a UAC prompt, and that copy does the job and exits. The program
as a whole never runs elevated (see `src/app.manifest`). The installer turns
a running server off by posting the tray menu's "Turn Dental Comms off"
command (`WM_COMMAND` 204) to its window, which every 0.9 version
understands, so that id must never change.

Everything the checklist says comes from `buildChecks()` in `checks.cpp`, a
function from a plain `Facts` struct to rows. The self-test drives it through
every state and fails if any problem row lacks an explanation or reads like
an error code.

On a Public network the restriction is a `bind()` to `127.0.0.1` rather than
every interface, so it is the operating system refusing the connection, not
Dental Comms choosing what to answer. That also means it needs no firewall
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

Note for anyone working on the browser side: staff reach Dental Comms over plain
`http://`, which is **not** a secure context. `crypto.randomUUID`,
`crypto.subtle`, service workers and the Notification API are all unavailable
there — they work on the server computer's own `localhost` and nowhere else, so
testing only on the server machine will not show it. `crypto.getRandomValues`
is fine, and `public/admin.html` builds its ids from that.

`server.js.reference` is the original Node implementation, kept for comparison.
It predates everything in the two paragraphs above.

---

## Code signing policy

**Releases are not signed yet.** Dental Comms has applied for free code
signing from [SignPath Foundation](https://signpath.org), which asks for an
established project first. Until it is approved, Windows shows release files
as coming from an unknown publisher: SmartScreen warns before running them
(**More info → Run anyway**), and Smart App Control may refuse them. What
follows is how signing will work once it is in place.

Free code signing will be provided by [SignPath.io](https://about.signpath.io),
certificate by [SignPath Foundation](https://signpath.org).

Windows will show the publisher of a signed release as **SignPath Foundation**.
Only the files attached to a
[GitHub release](https://github.com/SMEdevelop/DentalComms/releases) will be
signed, and only after they have been built from the tagged commit by the
release workflow on a GitHub-hosted runner. Anything you build yourself with
`build.bat` or `package.bat` is outside this policy.

**Team roles**

- Committers and reviewers: [SMEdevelop](https://github.com/SMEdevelop)
- Approvers: [SMEdevelop](https://github.com/SMEdevelop)

Changes from anyone else arrive as pull requests and are reviewed by a
committer before they are merged.

**Privacy policy**

This program will not transfer any information to other networked systems
unless specifically requested by the user or the person installing or
operating it. Dental Comms talks only to the browsers on your own office
network that open it: no telemetry, no update checks, no accounts, and nothing
sent to the internet. Messages and settings stay on the computer running it,
in `data/config.json` and in memory.

---

## License and disclaimer

Dental Comms is open source under the [MIT License](LICENSE).
Copyright © 2026 SMEdevelop.

`public/qrcode.js` (used by the printable setup card) is
[QR Code Generator](https://github.com/kazuhikoarase/qrcode-generator) by
Kazuhiko Arase, also under the MIT License; its notice is at the top of the
file. "QR Code" is a registered trademark of DENSO WAVE INCORPORATED.

You may use, copy, modify, merge, publish, distribute, sublicense and sell it,
provided the copyright notice and the license text travel with it.

It is provided **as is**, with no warranty of any kind and no liability for any
loss arising from a message that was delayed, misdirected, or never seen. It is
not a medical device and must not be relied on for anything urgent. The full
terms are in [LICENSE](LICENSE).
