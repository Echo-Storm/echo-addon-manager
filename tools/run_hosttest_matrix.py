"""Runs the offline test host (nr_hosttest.exe) through a set of scenarios and checks the presented frame it writes.

No window is shown and nothing touches Lossless Scaling or a game: the host loads the built addon DLL, feeds it a synthetic
LSFG pattern and a hidden swap chain, and dumps one presented (generated) frame to present_gen.bmp. Every scenario compares
that frame with the known synthetic pattern (or with the baseline scenario).

It uses the GPU for about half a minute per scenario, so do not run it while a game is running.

    python run_hosttest_matrix.py [--nr <addon build folder>] [--snippet <path to nvngx_dlssnr.dll>]
                                  [--out OUTDIR] [--only name,name] [--list]
"""
import argparse
import re
import os
import shutil
import subprocess
import sys
import time

import numpy as np

W, H = 1920, 1080
DEFAULT_NR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'addons', 'DLSS5NR01', 'build', 'Release')
DEFAULT_SNIPPET = os.path.join(os.environ.get('LS_DIR', r'C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling'), 'nvngx_dlssnr.dll')


def pattern():
    """The synthetic frame the host feeds LSFG (see Pattern() in addon_host_test.cpp), as float RGB 0..255, shape (H, W, 3)."""
    x = np.arange(W)[None, :].repeat(H, 0)
    y = np.arange(H)[:, None].repeat(W, 1)
    r = (x * 255 // W).astype(np.float32)
    g = (y * 255 // H).astype(np.float32)
    b = np.where(((x // 32 + y // 32) & 1) == 1, 200, 60).astype(np.float32)
    return np.stack([r, g, b], -1)


def load_bmp(path):
    """32-bit top-down BMP written by the host (BGRA) -> float RGB (H, W, 3)."""
    with open(path, 'rb') as f:
        data = f.read()
    off = int.from_bytes(data[10:14], 'little')
    w = int.from_bytes(data[18:22], 'little', signed=True)
    h = abs(int.from_bytes(data[22:26], 'little', signed=True))
    px = np.frombuffer(data, np.uint8, count=w * h * 4, offset=off).reshape(h, w, 4)
    return px[..., [2, 1, 0]].astype(np.float32)


def luma(a):
    return a @ np.array([0.299, 0.587, 0.114], np.float32)


class Result:
    def __init__(self):
        self.lines = []
        self.ok = True

    def check(self, name, cond, detail=''):
        self.lines.append(('PASS' if cond else 'FAIL') + '  ' + name + (('  (' + detail + ')') if detail else ''))
        self.ok &= bool(cond)


def run_host(nr_dir, snippet, keys, out_dir, tag):
    exe = os.path.join(nr_dir, 'nr_hosttest.exe')
    # addon=<dll> picks the addon of the pair to load (DLSS 5 Neural Rendering by default); every other key goes to the host
    dll = next((k[6:] for k in keys if k.startswith('addon=')), 'DLSS5NR01.dll')
    args = [exe, dll, '-', snippet] + [k for k in keys if not k.startswith('addon=')]
    for stale in ('present_gen.bmp', 'present_real.bmp'):
        p = os.path.join(nr_dir, stale)
        if os.path.exists(p):
            os.remove(p)
    t0 = time.time()
    proc = subprocess.run(args, cwd=nr_dir, capture_output=True, text=True, timeout=240)
    text = proc.stdout + proc.stderr
    with open(os.path.join(out_dir, tag + '.log'), 'w', encoding='utf-8') as f:
        f.write(text)
    bmp = os.path.join(nr_dir, 'present_gen.bmp')
    frame = None
    if os.path.exists(bmp):
        shutil.copyfile(bmp, os.path.join(out_dir, tag + '.bmp'))
        frame = load_bmp(bmp)
    return proc.returncode, text, frame, time.time() - t0


def basic(res, rc, text):
    res.check('exit code 0', rc == 0, 'rc=%s' % rc)
    res.check('tap is read-only', 'TAP READ-ONLY' in text)
    res.check('no crash / engine failure', 'NrEngine FAILED' not in text and 'CRASH' not in text and 'DISABLED' not in text)


def motion_counts(text):
    """The last 'motion vectors so far' line: (this frame's flow, the previous frame's, dropped waiting), or None."""
    import re
    m = re.findall(r"motion vectors so far: this frame's flow (\d+), the previous frame's (\d+), frames dropped waiting for a flow pass (\d+)", text)
    return tuple(int(v) for v in m[-1]) if m else None


def scenario_base(ctx, res, text, frame):
    res.check('compose applied', 'COMPOSE APPLIED' in text)
    mc = motion_counts(text)
    res.check("the model gets this frame's own motion (fresh flow, the default)", bool(mc) and mc[0] > 0 and mc[0] >= 9 * (mc[0] + mc[1]) // 10 and mc[2] <= 2,
              'fresh %d, previous %d, dropped %d' % mc if mc else 'no motion line in the log')
    res.check('tap followed the resolution change', 'TAP FOLLOWED' in text)
    held = re.search(r'held up a dispatch while the model was made again: ([0-9.]+ ms)', text)
    res.check('making the model again for the new size did not hold up the render thread', 'NO STALL' in text, held.group(1) if held else 'no line in the log')
    res.check('with frame generation off, the last result does not stay on the screen', 'OLD RESULT DROPPED' in text)
    res.check('the addon reports live metrics and a status to the host', 'LIVE METRICS OK' in text)
    d = np.abs(frame - ctx['pat']).mean()
    ctx['base'] = frame
    res.check('model changed the picture', d > 0.3, 'mean abs change %.2f' % d)


def scenario_hud(ctx, res, text, frame):
    left = np.abs(frame[:, : W // 2 - 8] - ctx['pat'][:, : W // 2 - 8]).max()
    right = np.abs(frame[:, W // 2 + 8:] - ctx['pat'][:, W // 2 + 8:]).mean()
    res.check('protected left half is bit-identical to the original', left == 0, 'max diff %.1f' % left)
    res.check('unprotected right half is still enhanced', right > 0.3, 'mean abs change %.2f' % right)


def scenario_sharpen(ctx, res, text, frame):
    def hf(a):  # high-frequency energy of the luma
        l = luma(a)
        return np.abs(l[:, 1:] - l[:, :-1]).mean()
    res.check('sharpen adds high-frequency energy', hf(frame) > hf(ctx['base']) * 1.02, '%.3f vs %.3f' % (hf(frame), hf(ctx['base'])))


def scenario_shadows_up(ctx, res, text, frame):
    lp, lb, lf = luma(ctx['pat']), luma(ctx['base']), luma(frame)
    dark, bright = lp < 60, lp > 190
    gd, gb = (lf[dark] - lb[dark]).mean(), (lf[bright] - lb[bright]).mean()
    res.check('shadows +1 lifts the dark areas', gd > 8, 'mean luma change %.1f' % gd)
    res.check('shadows +1 leaves the bright areas alone', abs(gb) < 3, 'mean luma change %.1f' % gb)


def scenario_highlights_down(ctx, res, text, frame):
    lp, lb, lf = luma(ctx['pat']), luma(ctx['base']), luma(frame)
    dark, bright = lp < 60, lp > 190
    gb, gd = (lf[bright] - lb[bright]).mean(), (lf[dark] - lb[dark]).mean()
    res.check('highlights -1 pulls the bright areas down', gb < -8, 'mean luma change %.1f' % gb)
    res.check('highlights -1 leaves the dark areas alone', abs(gd) < 3, 'mean luma change %.1f' % gd)


def scenario_grain(ctx, res, text, frame):
    diff = (luma(frame) - luma(ctx['base']))
    res.check('grain adds noise', diff.std() > 3, 'std of change %.2f' % diff.std())
    res.check('grain averages out (mean change small)', abs(diff.mean()) < 3, 'mean %.2f' % diff.mean())


def scenario_grain_hud(ctx, res, text, frame):
    left = np.abs(frame[:, : W // 2 - 8] - ctx['pat'][:, : W // 2 - 8]).max()
    res.check('grain does not touch a protected area', left == 0, 'max diff %.1f' % left)


def scenario_smooth(ctx, res, text, frame):
    d = np.abs(frame - ctx['pat']).mean()
    res.check('smoothed delta still changes the picture', d > 0.2, 'mean abs change %.2f' % d)
    res.check('smoothed delta is not wildly different from unsmoothed', abs(d - np.abs(ctx['base'] - ctx['pat']).mean()) < 3.0)


def scenario_smooth_passes(ctx, res, text, frame):
    # three model passes change the picture more than one (measured earlier: 6.6 / 12.5 / 17.6 for 1 / 2 / 3 passes), so the
    # comparison is "clearly stronger than one pass, and not absurd"
    d = np.abs(frame - ctx['pat']).mean()
    b = np.abs(ctx['base'] - ctx['pat']).mean()
    res.check('3 passes with smoothing change the picture more than 1 pass', d > b * 1.5, '%.2f vs %.2f' % (d, b))
    res.check('3 passes with smoothing stay in a sane range', d < 40, 'mean abs change %.2f' % d)


def _halves(frame, pat):
    """Mean absolute change against the pattern in the left and right half (clear of the middle and the edges)."""
    l = np.abs(frame[:, 16: W // 2 - 16] - pat[:, 16: W // 2 - 16]).mean()
    r = np.abs(frame[:, W // 2 + 16: W - 16] - pat[:, W // 2 + 16: W - 16]).mean()
    return l, r


def scenario_ghost_off(ctx, res, text, frame):
    l, r = _halves(frame, ctx['pat'])
    ctx['ghost_off'] = (l, r)
    res.check('guard off: both halves are enhanced', l > 0.3 and r > 0.3, 'left %.2f right %.2f' % (l, r))


def scenario_ghost_on(ctx, res, text, frame):
    l, r = _halves(frame, ctx['pat'])
    lo, ro = ctx['ghost_off']
    res.check('guard on: the half whose motion fields agree keeps its enhancement', l > 0.75 * lo, '%.2f vs %.2f without the guard' % (l, lo))
    res.check('guard on: the half whose motion fields disagree is faded out', r < 0.25 * ro, '%.2f vs %.2f without the guard' % (r, ro))


def scenario_none(ctx, res, text, frame):
    pass


def scenario_flow_previous(ctx, res, text, frame):
    mc = motion_counts(text)
    res.check("with fresh flow off the model gets the previous frame's motion, as before", bool(mc) and mc[0] == 0 and mc[1] > 0,
              'fresh %d, previous %d, dropped %d' % mc if mc else 'no motion line in the log')
    res.check('...and it still changes the picture', np.abs(frame - ctx['pat']).mean() > 0.3)


def scenario_scaler(ctx, res, text, frame):
    # the DLSS 4 Upscaler in place of Lossless Scaling's NIS pass (a fake one here, which paints its output magenta)
    made = re.search(r'DLSS scaler: (\d+)x(\d+) -> (\d+)x(\d+).*?made in (\d+) ms', text)
    res.check('it finds the NIS pass and makes DLSS for its sizes', bool(made) and made.group(1, 2, 3, 4) == ('1920', '1080', '2880', '1620'),
              made.group(0) if made else 'no line in the log')
    nis = re.search(r'\[check-nis\].*', text)
    res.check('DLSS writes a real upscaled picture where the NIS pass would have', 'DLSS REPLACED NIS' in text, nis.group(0)[12:] if nis else 'no check line')
    res.check('...on every presented frame, real and generated', bool(re.search(r'DLSS scaler: \d+ frames upscaled, NIS passes seen \d+, 2 per real frame', text)))


def scenario_pair(ctx, res, text, frame):
    # both addons loaded and switched on: the upscaler works beside Neural Rendering, which keeps its frames
    res.check('neither steps aside', 'BOTH ON' in text)
    res.check('Neural Rendering works on the frames as usual', 'COMPOSE APPLIED' in text and frame is not None and np.abs(frame - ctx['pat']).mean() > 0.3)


def scenario_selftest(ctx, res, text, frame):
    # the addon's own 'Test compatibility' path (started at start-up by the selfTestOnStart switch): nr_selftest.exe runs the model in its own process
    res.check('the addon ran the compatibility test and it passed with this model', 'compatibility test: passed (PASS)' in text)


# name, config overrides, checker
SCENARIOS = [
    ('base', [], scenario_base),
    ('hud_left_half', ['hud=0,0,0.5,1', 'hudFeather=0'], scenario_hud),
    ('sharpen', ['sharpen=0.8'], scenario_sharpen),
    ('shadows_up', ['shadows=1'], scenario_shadows_up),
    ('highlights_down', ['highlights=-1'], scenario_highlights_down),
    ('grain', ['grain=0.6', 'grainSize=1'], scenario_grain),
    ('grain_inside_hud', ['grain=0.6', 'hud=0,0,0.5,1', 'hudFeather=0'], scenario_grain_hud),
    ('smooth', ['deltaSmooth=0.5'], scenario_smooth),
    ('smooth_passes3', ['deltaSmooth=0.5', 'passes=3'], scenario_smooth_passes),
    ('ghost_off', ['flowsplit=1', 'ghostGuard=0'], scenario_ghost_off),
    ('ghost_on', ['flowsplit=1', 'ghostGuard=1'], scenario_ghost_on),
    ('flow_previous', ['freshFlow=0'], scenario_flow_previous),
    ('selftest', ['selfTestOnStart=1'], scenario_selftest),
    ('scaler', ['addon=DLSS4DLAA.dll', 'nis=1'], scenario_scaler),
    ('scaler_m', ['addon=DLSS4DLAA.dll', 'nis=1', 'dlaaPreset=13'], scenario_scaler),
    ('pair', ['second=DLSS4DLAA.dll'], scenario_pair),
    ('exit_abrupt', ['exitmode=abrupt'], scenario_none),   # the process ends with the addon loaded and no AddonShutdown, as Lossless Scaling does
    ('ui_shot', ['shot=@OUT@/ui_nr_panel.bmp', 'snapshotOnStart=1', 'hud=0,0,0.3,0.17/0.86,0,1,0.24', 'deltaSmooth=0.3', 'grain=0.2', 'shadows=0.2', 'presetNames=Night raid|Bright zone', 'preset.Night raid=shadows=0.4;grain=0.15', 'preset.Bright zone=highlights=-0.3;sharpen=0.2'], scenario_none),
]


# The everyday set (--quick): the frame reaching the model with its own motion, the older timing, an exit with no AddonShutdown, the DLSS 4
# Upscaler in place of NIS, and the two addons loaded together. The
# rest (looks, HUD, grain, smoothing, the self-test, the upscaler with preset M, the panel shot) run with no option, before a release.
QUICK = {'base', 'flow_previous', 'exit_abrupt', 'scaler', 'pair'}


def selftest_exe_checks(nr_dir, snippet):
    """Runs nr_selftest.exe directly: the real model must pass (exit 0), and each way of being unusable must end with its own code, never a crash."""
    exe = os.path.join(nr_dir, 'nr_selftest.exe')
    print('== nr_selftest.exe')
    if not os.path.exists(exe):
        print('  FAIL  nr_selftest.exe is not built'); return 1
    import tempfile
    tmp = tempfile.mkdtemp(prefix='nr_selftest_')
    garbage = os.path.join(tmp, 'garbage.dll'); open(garbage, 'wb').write(b'MZ' + os.urandom(25 * 1024 * 1024))
    ls = os.path.dirname(snippet)
    cases = [
        ('the real model passes', [exe, '--model', snippet, '--lsdir', ls], 0),
        ('a missing model file is MODEL_LOAD (14)', [exe, '--model', os.path.join(tmp, 'absent.dll')], 14),
        ('25 MB of random bytes is MODEL_LOAD (14)', [exe, '--model', garbage], 14),
        ('no NVIDIA card with that LUID is NO_GPU (10)', [exe, '--luid', '7f:1234', '--model', snippet], 10),
    ]
    bad = 0
    for what, cmd, want in cases:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        lines = [l for l in p.stdout.splitlines() if l.startswith('SELFTEST ')]
        ok = p.returncode == want and bool(lines) and lines[-1].split()[1] == str(want)
        print('  %s  %s  (exit %s)' % ('PASS' if ok else 'FAIL', what, p.returncode)); bad += 0 if ok else 1

    # --report: a shareable text file, written whatever the result, that names the card, the driver and the model file's version and size, and never a folder
    for what, cmd, want in cases[:2]:
        rep = os.path.join(tmp, 'report_%d.txt' % want)
        p = subprocess.run(cmd + ['--report', rep], capture_output=True, text=True, timeout=180)
        text = open(rep, encoding='utf-8', errors='replace').read() if os.path.exists(rep) else ''
        row = [l for l in text.splitlines() if l.startswith('| ') and 'Card' not in l]
        checks = [
            ('is written', bool(text)),
            ('says the result', ('result:          %s' % ('PASS (code 0)' if want == 0 else 'MODEL_LOAD (code 14)')) in text),
            ('names the graphics card and a driver number', 'graphics card:' in text and 'NVIDIA driver:   ' in text and 'NVIDIA driver:   unknown' not in text),
            ('has the row for the compatibility table', len(row) == 1 and row[0].count('|') == 6 and ('PASS' if want == 0 else 'FAIL MODEL_LOAD') in row[0]),
            ('names no folder and no user name', chr(92) not in text and ':/' not in text and os.environ.get('USERNAME', '@@') not in text),
        ]
        if want == 0:
            checks.append(('gives the model file version and size', bool(__import__('re').search(r'model file:\s+nvngx_dlssnr\.dll, version [0-9.]+, [0-9.]+ MB', text))))
        for name, ok in checks:
            print('  %s  the report for "%s" %s' % ('PASS' if ok else 'FAIL', what, name)); bad += 0 if ok else 1
    return 1 if bad else 0


def panel_sections_closed_check():
    """Every collapsible section of the Neural Rendering panel must start closed: eam::ui::SectionHeader is called without its default-open argument."""
    import re
    src = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'addons', 'DLSS5NR01', 'src', 'addon', 'panel.cpp')
    text = open(src, encoding='utf-8', errors='replace').read()
    headers = re.findall(r'eam::ui::SectionHeader\(([^;{]*?)\)\)\s*\{', text)
    opens = [h for h in headers if re.search(r',\s*true\s*$', h.strip())]
    print('== panel sections')
    ok = len(headers) >= 6 and not opens
    print('  %s  all %d collapsible sections start closed%s' % ('PASS' if ok else 'FAIL', len(headers), '' if not opens else '  (open by default: %s)' % ', '.join(opens)))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nr', default=DEFAULT_NR)
    ap.add_argument('--snippet', default=DEFAULT_SNIPPET)
    ap.add_argument('--out', default=os.path.join(os.environ.get('TEMP', '.'), 'hosttest_matrix'))
    ap.add_argument('--only', default='')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--quick', action='store_true', help='only the everyday set: ' + ', '.join(sorted(QUICK)))
    a = ap.parse_args()
    if a.list:
        for n, k, _ in SCENARIOS:
            print(n, ' '.join(k))
        return 0
    os.makedirs(a.out, exist_ok=True)
    only = set(x for x in a.only.split(',') if x) | (QUICK if a.quick else set())
    ctx = {'pat': pattern()}
    failed = 0
    for name, keys, checker in SCENARIOS:
        if only and name not in only and name != 'base':
            continue
        keys = [k.replace('@OUT@', a.out.replace('\\', '/')) for k in keys]
        rc, text, frame, secs = run_host(a.nr, a.snippet, keys, a.out, name)
        res = Result()
        basic(res, rc, text)
        if frame is None and name != 'ui_shot':
            res.check('present_gen.bmp written', False)
        elif frame is not None:
            checker(ctx, res, text, frame)
        print('== %s  (%.0f s)  %s' % (name, secs, ' '.join(keys)))
        for ln in res.lines:
            print('  ' + ln)
        failed += 0 if res.ok else 1
    if not only:
        failed += selftest_exe_checks(a.nr, a.snippet)
        failed += panel_sections_closed_check()
    print('\n%s' % ('ALL SCENARIOS PASSED' if not failed else '%d SCENARIO(S) FAILED' % failed))
    print('logs and frames in', a.out)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
