r"""How much of the manager's source is still the code this project started from?

    python tools\measure_original_share.py <folder holding the original's src\ and sdk\> [more folders of original code ...] [--files]

Compares every .cpp and .h under manager\src and manager\sdk\include with a pool of the original's lines. A line counts as original when,
ignoring leading and trailing spaces, it appears in the original (each original line can be matched as many times as it occurs there), so code
that was moved between files still counts. Blank lines, lines that are only braces, and lines shorter than 4 characters are left out of both
sides, since they say nothing about who wrote the code. It is a rough measure, in the same spirit as `git blame -w -M -C`, and is meant to be
read in percent, not to the line.

To get the original, check out FrankBarretta/LosslessProxy at v0.3.0 (commit 6ca17e1) and point this at its LosslessProxy folder. The ReShade and Windowed
features were addons of the original project and now live in manager\src\features, so also give the folder holding those addons as first vendored
(its addons\ folder); their .cpp, .h and .hpp files join the pool of original lines.
"""
import collections
import os
import re
import sys

repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
mine = [os.path.join(repo, 'manager', 'src'), os.path.join(repo, 'manager', 'sdk', 'include')]
positional = [a for a in sys.argv[1:] if not a.startswith('--')]
theirs = positional[0] if positional else None
extra_folders = positional[1:]
show_files = '--files' in sys.argv
if not theirs or not os.path.isdir(theirs):
    print(__doc__)
    sys.exit(2)


def source_files(roots, exts=('.cpp', '.h')):
    for root in roots:
        for d, _, fs in os.walk(root):
            for f in fs:
                if f.endswith(exts):
                    yield os.path.join(d, f)


TRIVIAL = re.compile(r'^[{}();,\s]*$')


def lines_of(path):
    out = []
    with open(path, encoding='utf-8', errors='replace') as fh:
        for ln in fh:
            s = ln.strip()
            if len(s) >= 4 and not TRIVIAL.match(s):
                out.append(s)
    return out


pool = collections.Counter()
for p in source_files([os.path.join(theirs, 'LosslessProxy', 'src'), os.path.join(theirs, 'LosslessProxy', 'sdk')] if os.path.isdir(os.path.join(theirs, 'LosslessProxy')) else [os.path.join(theirs, 'src'), os.path.join(theirs, 'sdk')]):
    pool.update(lines_of(p))
for p in source_files(extra_folders, ('.cpp', '.h', '.hpp')):
    pool.update(lines_of(p))
orig_total = sum(pool.values())

rows, total, same = [], 0, 0
left = pool.copy()
for p in sorted(source_files(mine)):
    ls = lines_of(p)
    n = len(ls)
    m = 0
    for s in ls:
        if left[s] > 0:
            left[s] -= 1
            m += 1
    total += n
    same += m
    rows.append((m, n, os.path.relpath(p, os.path.join(repo, 'manager'))))

if show_files:
    for m, n, rel in sorted(rows, key=lambda r: -r[0]):
        if m:
            print('%4d / %4d  %3.0f%%  %s' % (m, n, 100.0 * m / n, rel))
print('manager source now: %d lines, %d of them original (%.1f%%); the original had %d' % (total, same, 100.0 * same / max(total, 1), orig_total))
