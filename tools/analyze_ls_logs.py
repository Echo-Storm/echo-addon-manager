"""Summarises DLSS5NR01.log (in the Lossless Scaling folder's logs\\ directory): how the game's frame time was
distributed and what the model cost, from the every-300-frames lines the addon writes.

    python analyze_ls_logs.py [path\\to\\DLSS5NR01.log]
"""
import os
import re
import statistics
import sys

DEFAULT = os.path.join(os.environ.get('LS_DIR', r'C:\Program Files (x86)\Steam\steamapps\common\Lossless Scaling'), 'logs', 'DLSS5NR01.log')
path = sys.argv[1] if len(sys.argv) > 1 else DEFAULT

pat = re.compile(
    r'\[(?P<t>[\d:.]+)\] frame time over the last (?P<n>\d+) frames: p50 (?P<p50>[\d.]+) ms, p95 (?P<p95>[\d.]+), p99 (?P<p99>[\d.]+), '
    r'worst (?P<worst>[\d.]+) \| (?P<o20>\d+) frames over 20 ms \((?P<o20p>\d+)%\), (?P<o33>\d+) over 33 ms \| model (?P<model>[\d.]+) ms')
rows = []
with open(path, encoding='utf-8', errors='replace') as f:
    text = f.read()
for m in pat.finditer(text):
    rows.append({k: (v if k == 't' else float(v)) for k, v in m.groupdict().items()})

print(path)
print('windows of ~300 frames:', len(rows))
if rows:
    def col(k):
        return [r[k] for r in rows]
    print('frame time p50   median %.1f ms   (%.1f fps)' % (statistics.median(col('p50')), 1000 / statistics.median(col('p50'))))
    print('frame time p95   median %.1f ms   worst window %.1f ms' % (statistics.median(col('p95')), max(col('p95'))))
    print('frame time p99   median %.1f ms   worst window %.1f ms' % (statistics.median(col('p99')), max(col('p99'))))
    print('frames over 20 ms: %.0f%% on average, worst window %.0f%%' % (statistics.mean(col('o20p')), max(col('o20p'))))
    print('model time       median %.1f ms   max %.1f ms' % (statistics.median(col('model')), max(col('model'))))
    bad = [r for r in rows if r['o20p'] > 20]
    print('windows with more than 20%% of frames over 20 ms: %d' % len(bad))
    for r in bad[:10]:
        print('   %s  p50 %.1f p99 %.1f  over20 %d%%' % (r['t'], r['p50'], r['p99'], r['o20p']))
for key in ('DISABLED', 'CRASH', 'FAILED', 'watchdog', 'exception'):
    n = len(re.findall(key, text))
    if n:
        print('log mentions "%s" %d time(s)' % (key, n))
m = re.findall(r'preset .*|game .* took focus.*', text)
for line in m[-5:]:
    print('  ' + line)
