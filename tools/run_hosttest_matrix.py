"""Runs the offline test host (lspnr_hosttest.exe) through a set of scenarios and checks the presented frame it writes.

No window is shown and nothing touches Lossless Scaling or a game: the host loads the built addon DLL, feeds it a synthetic
LSFG pattern and a hidden swap chain, and dumps one presented (generated) frame to present_gen.bmp. Every scenario compares
that frame with the known synthetic pattern (or with the baseline scenario).

It uses the GPU for about half a minute per scenario, so do not run it while a game is running.

    python run_hosttest_matrix.py [--nr <addon build folder>] [--snippet <path to nvngx_dlssnr.dll>]
                                  [--out OUTDIR] [--only name,name] [--list]
"""
import argparse
import os
import shutil
import subprocess
import sys
import time

import numpy as np

W, H = 1920, 1080
DEFAULT_NR = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), 'addons', 'LSP-NeuralRender', 'build', 'Release')
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
    exe = os.path.join(nr_dir, 'lspnr_hosttest.exe')
    args = [exe, 'LSP_NeuralRender.dll', '-', snippet] + keys
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


def scenario_base(ctx, res, text, frame):
    res.check('compose applied', 'COMPOSE APPLIED' in text)
    res.check('tap followed the resolution change', 'TAP FOLLOWED' in text)
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
    ('ui_shot', ['shot=@OUT@/ui_nr_panel.bmp', 'hud=0,0,0.3,0.17/0.86,0,1,0.24', 'deltaSmooth=0.3', 'grain=0.2', 'shadows=0.2', 'presetNames=Night raid|Bright zone', 'preset.Night raid=shadows=0.4;grain=0.15', 'preset.Bright zone=highlights=-0.3;sharpen=0.2'], scenario_none),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nr', default=DEFAULT_NR)
    ap.add_argument('--snippet', default=DEFAULT_SNIPPET)
    ap.add_argument('--out', default=os.path.join(os.environ.get('TEMP', '.'), 'hosttest_matrix'))
    ap.add_argument('--only', default='')
    ap.add_argument('--list', action='store_true')
    a = ap.parse_args()
    if a.list:
        for n, k, _ in SCENARIOS:
            print(n, ' '.join(k))
        return 0
    os.makedirs(a.out, exist_ok=True)
    only = set(x for x in a.only.split(',') if x)
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
    print('\n%s' % ('ALL SCENARIOS PASSED' if not failed else '%d SCENARIO(S) FAILED' % failed))
    print('logs and frames in', a.out)
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
