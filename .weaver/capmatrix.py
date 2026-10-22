"""One hidden (private desktop, volume 0) level run with an emulated external frame cap on the GS thread's present
(KZ_TEST_PRESENT_FPS / _SLEEP_US / _STALL, ext/kzgs). Wraps lv2hidden.py, then summarises the heartbeats in killzone.log.
Usage: python capmatrix.py <name> <seconds> [--level 2] [--exe <dir>] [--script "..."] [KEY=VALUE ...]
  e.g. python capmatrix.py cap100 200 KZ_FPS=144 KZ_TEST_PRESENT_FPS=100
Output: work/cap/<name>.log (copy of killzone.log), work/cap/<name>/gs_*.png every 10 s, one line per 10 s of heartbeat deltas."""
import os, re, shutil, subprocess, sys
name, seconds = sys.argv[1], sys.argv[2]
rest = sys.argv[3:]
exe_dir = r'D:\KillzoneRecomp\build\agentw'
for i, a in enumerate(rest):
    if a == '--exe': exe_dir = rest[i + 1]
shots = r'D:\KillzoneRecomp\work\cap\%s' % name
shutil.rmtree(shots, ignore_errors=True); os.makedirs(shots)
env = dict(os.environ, KZ_SHOT_DIR=shots, KZ_SHOT_INTERVAL='10')
log = os.path.join(exe_dir, 'killzone.log')
if os.path.exists(log): os.remove(log)
subprocess.run([sys.executable, r'D:\KillzoneRecomp\.weaver\lv2hidden.py', name, seconds] + rest, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
import glob
pngs = sorted(glob.glob(shots + r'\gs_*.png'))
if pngs: os.remove(pngs[-1])  # the kill can cut the last one
dst = r'D:\KillzoneRecomp\work\cap\%s.log' % name
shutil.copy(log, dst)
prev = None
for l in open(dst, errors='replace'):
    m = re.match(r'\[headless\] t=(\d+)s pc=(\S+) ra=(\S+) threads=(\d+) vsync=(\d+) dma=(\d+) gif=(\d+) gsw=(\d+) vif=(\d+)', l)
    if not m: continue
    t, vs, dma, vif = int(m[1]), int(m[5]), int(m[6]), int(m[9])
    if prev and t % 10 == 0:
        print('t=%3d pc=%s vsync+%d dma+%d vif+%d' % (t, m[2], vs - prev[1], dma - prev[2], vif - prev[3]))
    if t % 10 == 0 or prev is None: prev = (t, vs, dma, vif)
print('log bytes', os.path.getsize(dst))
