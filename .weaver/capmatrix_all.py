"""Frame-cap matrix: boot, menus, level load, gameplay, pause -> Restart (second level load), gameplay, for each emulated external cap
(KZ_TEST_PRESENT_*) and guest frame rate (KZ_FPS). Sequential (one exe dir, one killzone.log). Each run: work/cap/<tag>.log + <tag>/gs_*.png every 10 s
-> montage with tools/scripts/montage.py work/cap/<tag> 90 280. Usage: python capmatrix_all.py [tag ...]  (default: all)"""
import subprocess, sys
RESTART = "125:start:0.5;133:down:0.5;139:down:0.5;145:down:0.5;151:down:0.5;158:cross:0.5;166:cross:0.5;178:cross:0.5;190:cross:0.5"
CONFIGS = {
  'r144_none':   ['KZ_FPS=144'],
  'r144_c100':   ['KZ_FPS=144', 'KZ_TEST_PRESENT_FPS=100'],
  'r144_c60':    ['KZ_FPS=144', 'KZ_TEST_PRESENT_FPS=60'],
  'r144_c30':    ['KZ_FPS=144', 'KZ_TEST_PRESENT_FPS=30'],
  'r144_c144':   ['KZ_FPS=144', 'KZ_TEST_PRESENT_FPS=144'],
  'r60_c30':     ['KZ_FPS=60', 'KZ_TEST_PRESENT_FPS=30'],
  'r60_c60':     ['KZ_FPS=60', 'KZ_TEST_PRESENT_FPS=60'],
  'r60_c100':    ['KZ_FPS=60', 'KZ_TEST_PRESENT_FPS=100'],
  'r240_c100':   ['KZ_FPS=240', 'KZ_TEST_PRESENT_FPS=100'],
  'r240_c144':   ['KZ_FPS=240', 'KZ_TEST_PRESENT_FPS=144'],
  'r144_sleep10ms': ['KZ_FPS=144', 'KZ_TEST_PRESENT_SLEEP_US=10000'],
  'r144_stall':  ['KZ_FPS=144', 'KZ_TEST_PRESENT_FPS=100', 'KZ_TEST_PRESENT_STALL=50,300'],
  'r144_c100_OLDBLOCK': ['KZ_FPS=144', 'KZ_TEST_PRESENT_FPS=100', 'KZ_GS_BLOCK=1'],
}
tags = sys.argv[1:] or list(CONFIGS)
for tag in tags:
    r = subprocess.run([sys.executable, r'D:\KillzoneRecomp\.weaver\capmatrix.py', tag, '270', '--script', RESTART] + CONFIGS[tag], capture_output=True, text=True)
    print('==', tag); print(r.stdout[-1500:], flush=True)
