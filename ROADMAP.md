# Road to 1.0

What 1.0 should mean: someone who has never seen this project can **install it, keep it up to date, understand what it does and does not do, and get help**, without
editing files by hand, and the promises it makes (the addon API, the settings file, safety) are stable. This page is honest about where each part stands.

Status on 2026-09-23, at version 0.8.0 (the current work is on `main`).

| # | For 1.0 | State | Notes |
|---|---------|-------|-------|
| 1 | **An installer**: install, update, repair after a Lossless Scaling update, uninstall | **Done** (0.6.0) | `EchoAddonManagerSetup.exe`: one file, a small wizard, with the core (folder detection, backups, rollback, repair, uninstall) tested on fake folders and the exe tested end to end. Tried on a real install by the maintainer (the folder search, the pages, reinstall, and an update from 0.5.0 to 0.6.0, which worked); the "restart as administrator" path and Windows SmartScreen's reaction are not verified. See below. |
| 2 | **An update check** | **Done** (0.5.0) | Once a day, on by default, off in *Settings > Updates*; never downloads or installs; the only network access. |
| 3 | **A frozen, documented addon API** | **Done** on `main` | The API is at 1.0.0, documented (`docs/addon-authors.md`), with a written compatibility promise (`docs/api-compatibility.md`: what will and will not change before 2.0) and a sample addon (`examples/SampleAddon`) that builds against the SDK and is tested against the real manager. |
| 4 | **Every shipped feature checked in real use** | **Needs you** | Neural Rendering, the window, the tray and hotkey, and the exit path are checked live. Not yet: ReShade passthrough (hotkey and auto-click) and Windowed mode (the virtual display after a restart) have only passed offline tests. A short live try settles both. |
| 5 | **More than one game** | **Needs you** | Everything was developed and tested with World of Warcraft: Forever (the beta). A few other games, each with a line in the docs, would make the "tested with" note honest for more than one title. |
| 6 | **Signed files** | Not before 1.0 | Decided: the files stay unsigned until after 1.0. SmartScreen's warning and the way past it are explained in the README. |
| 7 | **Help when something goes wrong** | **Done** on `main` | A troubleshooting table in the README, a questions-and-answers page (`docs/faq.md`), the diagnostics zip and the log. More entries will come from real questions. |
| 8 | **Crash safety** | Done | Exit-path tests, isolated addon calls, the compatibility test in its own process, atomic settings writes. |
| 9 | **Model compatibility data** | **Done** on `main`, needs data | The self-test writes a shareable report (`--report`, and an *Open the compatibility report* button) and `docs/model-compatibility.md` is the table. It has one row (the maintainer's card); more come from other people's reports. |
| 10 | **Automated builds** | **Done** on `main` (green on GitHub) | `.github/workflows/build.yml` runs `tools/ci.ps1` (manager, installer and sample addon from a clean checkout, and the tests that need no GPU) on every push and pull request. Neural Rendering needs NVIDIA's SDK and the window tests need a desktop, so those stay on the maintainer's machine. |

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

## Done in the 0.8 interface pass

- Screenshots of the picture as it is shown, with a key and a folder of your choice (Neural Rendering's panel).
- Docking the manager beside Lossless Scaling's window.
- The version shown moved to 0.8.

## Ideas for after 1.0

Agreed as worth doing, in no particular order; none of them is started. They come after the hardening and optimization work toward 1.0.

- **A limiter in the Performance tab.** Limits that keep the GPU under a ceiling: load (%), temperature (°C), power (W) or graphics memory, and more than one at once ("under 80 °C *or* under 90 %",
  whichever is hit first). When one is reached the manager lowers the cost of what it controls (Neural Rendering's working scale and passes first) until the reading is back under it, and says which limit is
  active ("limiting: 83 °C"), so it never looks like unexplained stutter. Steps down and back up slowly, with a pause after each step, so it does not swing back and forth.
- **An auto mode for DLSS 5 Neural Rendering.** A budget ("the model may use 4 ms a frame", or "keep 20 % of the GPU free") that the addon keeps to by adjusting working scale and passes, from the model
  time and GPU headroom it already measures. It never touches the look (the style and picture sliders). Room to grow: per-game budgets, a quality floor it never goes under, pausing during loading screens,
  and a short history of what it changed and why.
- **Per-game profiles, applied automatically.** Neural Rendering already notices which game runs; the manager's own settings and the limiter could follow the same way.
- **A display mode per game.** Many TVs run 120 Hz only below 4K (1440p 120 Hz against 4K 60 Hz). A per-game choice (for example "World of Warcraft: Forever: 2560×1440 at 120 Hz")
  that the manager switches to when the game starts scaling and puts back when it ends, with Lossless Scaling's frame generation target to match.
- **A before/after capture.** One key saves a matching pair of screenshots with and without Neural Rendering, for comparing looks and for bug reports.
- **A session summary.** When a game closes: average and worst frame time, peak temperature and power, how long the limiter or the auto mode was active. Real numbers for the "tested with" list.
- **A stuck-state watchdog.** If Lossless Scaling's frames stop arriving while a game runs, say so and offer to restart Neural Rendering's engine, instead of leaving the person to guess.
- **A much easier HUD protection.** Today the protected areas are typed as numbers (or filled from the World of Warcraft starter layout), which is cumbersome. Better: draw and drag the
  areas over a live snapshot of the game in the panel, see them outlined in the game while editing, find likely HUD areas automatically (parts of the picture that stay put while the scene
  moves, which LSFG's own flow already shows), and keep a HUD layout per game with that game's look.
- **A DLSS 4.5 addon** next to Neural Rendering, for games where DLSS 5's look is not wanted. To look into first: which DLSS 4.5 features can work from what Lossless Scaling has (the captured frames and
  LSFG's optical flow, but no depth and no game motion vectors), and what NVIDIA's public SDK licence allows.
- **Watching: openNR** (github.com/clshortfuse/openNR). It rebuilds a DLSS NR compatibility DLL from a person's own `nvngx_dlssnr.dll`, so it runs the same model, not a better one.
  It is early, with no quality or performance claims yet. Nothing to build here: if it ever produces a working DLL, the person points Neural Rendering at it and runs **Test compatibility**.
  We never ship, host or link NVIDIA's files, or files made from them.

## Not planned for 1.0

Downloading or bundling the DLSS 5 model file, in any form. Automatic installation of updates. Support for anything other than Windows and Lossless Scaling.
