# Road to 1.0

What 1.0 should mean: someone who has never seen this project can **install it, keep it up to date, understand what it does and does not do, and get help**, without
editing files by hand, and the promises it makes (the addon API, the settings file, safety) are stable. This page is honest about where each part stands.

Status on 2026-09-21, at version 0.6.0 (the current work is on `main`).

| # | For 1.0 | State | Notes |
|---|---------|-------|-------|
| 1 | **An installer**: install, update, repair after a Lossless Scaling update, uninstall | **Done** (0.6.0) | `EchoAddonManagerSetup.exe`: one file, a small wizard, with the core (folder detection, backups, rollback, repair, uninstall) tested on fake folders and the exe tested end to end. Tried on a real install by the maintainer (the folder search, the pages, reinstall, and an update from 0.5.0 to 0.6.0, which worked); the "restart as administrator" path and Windows SmartScreen's reaction are not verified. See below. |
| 2 | **An update check** | **Done** (0.5.0) | Once a day, on by default, off in *Settings > Updates*; never downloads or installs; the only network access. |
| 3 | **A frozen, documented addon API** | **Done** on `main` | The API is at 1.0.0, documented (`docs/addon-authors.md`), with a written compatibility promise (`docs/api-compatibility.md`: what will and will not change before 2.0) and a sample addon (`examples/SampleAddon`) that builds against the SDK and is tested against the real manager. |
| 4 | **Every shipped feature checked in real use** | **Needs you** | Neural Rendering, the window, the tray and hotkey, and the exit path are checked live. Not yet: ReShade passthrough (hotkey and auto-click) and Windowed mode (the virtual display after a restart) have only passed offline tests. A short live try settles both. |
| 5 | **More than one game** | **Needs you** | Everything was developed and tested with World of Warcraft: Forever (the beta). A few other games, each with a line in the docs, would make the "tested with" note honest for more than one title. |
| 6 | **Signed files** | **Needs you** | Unsigned files make antivirus and SmartScreen nervous (`nr_selftest.exe` and the installer most of all). Free signing for open-source projects exists (SignPath); it needs the maintainer to apply. Optional, but it removes the biggest source of friction. |
| 7 | **Help when something goes wrong** | Open | A short troubleshooting page (the ten most likely problems and what to do) and an FAQ; the diagnostics zip and the log already exist. |
| 8 | **Crash safety** | Done | Exit-path tests, isolated addon calls, the compatibility test in its own process, atomic settings writes. |
| 9 | **Model compatibility data** | Open, optional | A shareable self-test report and a table of model builds known to work on which cards; the self-test already gives the ground truth for one machine. |
| 10 | **Automated builds** | Open, optional | A GitHub Actions run that builds and runs the tests that need no GPU on every push. |

## The installer, in detail

What it has to do (and what it must never do):

- **Find the Lossless Scaling folder**: from a running `LosslessScaling.exe`, Steam's library folders, the usual locations and the last folder used, or let the person pick it.
  Non-Steam copies are common, so picking must always work.
- **Refuse while Lossless Scaling is running**, and say why.
- **Tell whose `Lossless.dll` is whose without loading it.** Our `Lossless.dll` carries a version resource (product "Echo Addon Manager"); Lossless Scaling's says "Lossless Scaling".
- **Install** by backing up, renaming the original `Lossless.dll` to `Lossless_original.dll`, putting ours in its place, and copying the addons and icons. It never touches the person's
  `config.json`, their other addons or `addons\.removed`. Everything is verified by hash afterwards, and any failure rolls the folder back to how it was.
- **Repair after a Lossless Scaling update**, which puts its own `Lossless.dll` back and removes ours: keep the new original as `Lossless_original.dll` and put ours in again.
- **Uninstall**: put the original back; leave the addons and settings unless asked.
- **Say the one thing people must do themselves**: provide their own `nvngx_dlssnr.dll` (never shipped, never downloaded), and offer to place it.
- **Be honest about being unsigned**, and be a single file.

## Not planned for 1.0

Downloading or bundling the DLSS 5 model file, in any form. Automatic installation of updates. Support for anything other than Windows and Lossless Scaling.
