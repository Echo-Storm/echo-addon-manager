r"""Compares two folders of offscreen UI renders (the .bmp files ui_preview.ps1 makes), file by file.

    python tools\compare_ui_renders.py <before> <after>

Prints every picture that differs and how many pixels do, and exits 1 if any does. Rewriting window code without changing how it looks is
checked this way: render before, change the code, render after, compare.
"""
import os
import sys

import numpy as np
from PIL import Image

if len(sys.argv) != 3:
    print(__doc__)
    sys.exit(2)
a, b = sys.argv[1], sys.argv[2]


def rel_files(root):
    out = []
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.lower().endswith('.bmp'):
                out.append(os.path.relpath(os.path.join(d, f), root))
    return sorted(out)


fa, fb = rel_files(a), rel_files(b)
bad = 0
for rel in sorted(set(fa) | set(fb)):
    if rel not in fa or rel not in fb:
        print('only in %s: %s' % ('before' if rel in fa else 'after', rel))
        bad += 1
        continue
    ia = np.asarray(Image.open(os.path.join(a, rel)).convert('RGB'))
    ib = np.asarray(Image.open(os.path.join(b, rel)).convert('RGB'))
    if ia.shape != ib.shape:
        print('size differs: %s  %s vs %s' % (rel, ia.shape, ib.shape))
        bad += 1
        continue
    n = int((ia != ib).any(axis=2).sum())
    if n:
        ys, xs = np.nonzero((ia != ib).any(axis=2))
        print('%d pixels differ: %s  (rows %d..%d, columns %d..%d)' % (n, rel, ys.min(), ys.max(), xs.min(), xs.max()))
        bad += 1
print('%d of %d pictures differ' % (bad, len(set(fa) | set(fb))))
sys.exit(1 if bad else 0)
