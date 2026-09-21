# Questions and answers

Short answers to the things people ask most. The [README](../README.md) has the overview and a troubleshooting table; Neural Rendering has its own [user guide](../addons/DLSS5NR01/docs/user-guide.md) with a longer troubleshooting section.

## Before you install

**What does it actually do to my Lossless Scaling?**
Lossless Scaling loads a file called `Lossless.dll`. Echo Addon Manager takes that name; Lossless Scaling's own file is kept as `Lossless_original.dll`, and the manager passes everything on to it. Nothing in Lossless Scaling is patched
on disk, and uninstalling puts its own file back.

**Does it touch my game?**
No. It runs inside Lossless Scaling, not inside the game. (Whether a game's anti-cheat has an opinion about Lossless Scaling itself is between you and the game; the manager does not change that, and cannot promise anything about it.)

**Which versions and games has it been tested with?**
Lossless Scaling **3.2.2.0** on Windows 11 with an RTX 4070 Ti SUPER, and the game **World of Warcraft: Forever** (the beta). Other versions, cards and games are untested. If you try one, tell us how it went.

**Steam or non-Steam Lossless Scaling?**
Either. Setup looks in Steam libraries and in usual places on every drive, and lets you pick any folder. The copy it was developed with is not from Steam.

**Is it free? Is it safe to download?**
It is free and MIT-licensed, and the source is in this repository. The files are **not signed**, because signing costs money and free signing for open-source projects needs an application; Windows SmartScreen and some antivirus programs therefore
distrust them. You can build everything yourself from the source (see the README).

**Does it send anything over the internet?**
Once a day it asks github.com for the latest release number, and that is all: GitHub sees your IP address and the program's name and version. It never downloads or installs anything. You can turn it off in *Settings > Updates*. Setup itself uses no network at all.

## Setup

**Setup says Lossless Scaling is running, but I closed the window.**
Make sure it has really quit: look for its icon in the notification area, and for `LosslessScaling.exe` in Task Manager, and close it there. Then press *Check again*.

**Setup did not find my folder.**
Choose *Use a different folder...*, then *Browse for the folder...*, and pick the folder that holds `LosslessScaling.exe`. If you pick the folder above it, Setup uses the Lossless Scaling folder inside it when there is exactly one. Setup remembers your choice.

**Why does Setup want administrator rights?**
Only when the folder is somewhere Windows protects, such as under `C:\Program Files`. Otherwise it runs as you. If you would rather not, use a copy of Lossless Scaling outside `Program Files`.

**I updated Lossless Scaling and the manager is gone.**
Lossless Scaling put its own `Lossless.dll` back. Run Setup: it offers *Repair*, keeps the new original, and puts the manager back. Your settings are untouched.

**Can I undo it?**
Yes. *Uninstall* in Setup puts Lossless Scaling's own file back (your addons and settings stay, or go to the backups folder if you choose that). Everything Setup replaces is copied to `backups\` inside the Lossless Scaling folder first.

**Can I run Setup from a script?**
Yes: `EchoAddonManagerSetup.exe --silent install --folder "<folder>"` (or `uninstall`, `status`), with `--log <file>` to get the messages. The exit code is 0 when it worked.

## Using the manager

**Where did the window go?**
It hides to the notification area when you close it (Windows may put the icon under the ^ arrow). Click the icon, or press **Ctrl+Shift+F12**. *Settings* lets you change the key and choose whether the window opens when Lossless Scaling starts.

**Where are my settings?**
In `addons\config.json` inside the Lossless Scaling folder. *Settings > Save settings to a file* saves everything (the manager's and every addon's) to one file you can load back later or on another PC.

**A log line says `[Crash] unhandled exception` and Lossless Scaling closed.**
The manager writes a backtrace when the process it lives in crashes, so that the crash is not a mystery; it records the crash, it does not cause it. Frames in `coreclr.dll` are Lossless Scaling's own .NET code. Send the log with a bug report (below) if it happens again.

**Why is the Performance tab's GPU data missing?**
GPU load, power and temperature come from NVIDIA's driver library, so they only exist on NVIDIA cards, and are only read while the tab is open.

**Windowed mode or ReShade passthrough does nothing.**
Both are switched off until you turn them on in the *Features* tab. Windowed mode's virtual display has to exist before Lossless Scaling starts, so restart Lossless Scaling after switching it on.

## DLSS 5 Neural Rendering

**Where do I get `nvngx_dlssnr.dll`?**
This project does not distribute it, does not download it and does not say where to find it. You provide your own copy, and put it next to `LosslessScaling.exe` (Setup and the addon's *Browse for the model file...* button can copy it there). Everything else works without it.

**What does "Test compatibility" tell me?**
It runs your model file once, in a separate program, on your graphics card, and says whether it loads, creates its feature and changes a test picture. It is the way to find out before playing that a model build cannot run on your card. It does not prove the picture looks right in every game.

**Can I share what worked on my card?**
Yes, please: see [model-compatibility.md](model-compatibility.md).

## Reporting a problem

Open an issue at <https://github.com/Echo-Storm/echo-addon-manager/issues>. Attach the **diagnostics file** (*Settings > Create a diagnostics file*): a zip of your logs and settings that is only made on your PC and uploaded nowhere. Say what you did, what you expected, what happened, your Windows version, graphics card and Lossless Scaling version.
