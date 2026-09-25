# Tools

Scripts to build, test, look at and package LS Addon Manager. They find the repository from their own location, so run them from anywhere.
Where a script needs your Lossless Scaling folder it takes `-LsDir` (or reads the `LS_DIR` environment variable); the default is the usual Steam path.

**Rules the tools follow**

- Nothing here needs a visible window. Testing uses an offline host and an offscreen renderer, so it never takes over your screen.
- `deploy.ps1` never replaces files under a running game: it refuses while the game or Lossless Scaling runs, and backs up what it overwrites.
- The GPU test (`run_hosttest_matrix.py`) loads the GPU for about half a minute per scenario. Do not run it while a game runs.

| Tool | What it does |
|---|---|
| `build_all.ps1` | Configures (first time) and builds the manager, the Neural Rendering addon, the UI preview and the offline test host, Release x64. `-Only host,nr`. |
| `run_addon_tests.ps1` | Offline tests, no game needed, in suites (core, features, sample, update, gui, installer, setupexe, nr). By default it builds and runs only the suites that the files changed since the last commit can affect, and prints only failures and the time each suite took. `-Only core,gui` picks suites, `-All` runs every one (before a release), `-List` shows what starts each. |
| `run_hosttest_matrix.py` | `--quick`: the everyday set (base, the older motion timing, an exit with no shutdown). With no option, runs the Neural Rendering test host through every scenario (base compose, HUD protection, sharpen, shadows, highlights, grain, smoothing and more) and checks the presented frame against a synthetic pattern. Needs `nvngx_dlssnr.dll` (`--snippet` or `LS_DIR`). |
| `ui_preview.ps1` | Renders every tab, the addon cards and the addon panels offscreen to PNG. Set `EAM_PREVIEW_CLEAN=1` for the tidy scene used in the README. |
| `make_readme_shots.py` | Crops those renders into `docs/images`. |
| `compare_ui_renders.py` | Compares two folders of offscreen renders picture by picture (used to prove a rewrite of window code changed nothing you can see). |
| `measure_original_share.py` | How much of `manager/src` and `manager/sdk` is still the code this project started from, against a checkout of the original (see NOTICE.md). |
| `package.ps1` | Builds the release zip in `dist\` (`-Version`, `-SkipBuild`). Never packages NVIDIA's SDK or the DLSSNR snippet. |
| `deploy.ps1` | Copies a build into a Lossless Scaling folder, with backups. `-What host\|nr\|reshade\|windowed\|all`. |
| `analyze_ls_logs.py` | Summarises `DLSS5NR01.log`: frame-time distribution, model cost, presets applied. |
| `gpu_logger.ps1` | `nvidia-smi` once a second to a CSV; stops itself after 45 minutes. |
| `make_echo_icon.py` | Draws the Echo icon (`manager/manager-icon.ico` and `.png`), each size on its own. |
| `bmp2png.py` | BMP to PNG for a folder (needs Pillow). |
