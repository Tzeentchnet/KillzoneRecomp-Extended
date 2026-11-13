#!/usr/bin/env python3
"""Frame-pipeline timeline analysis (patch 0025). Input: the CSV written with KZ_TIMELINE=<file> (t_us,lane,event,a,b,c).

  tl_analyze.py <csv> [--region us|pal|eu] [--vblank-hz HZ] [--window-s START END] summary and frame breakdown
  tl_analyze.py <csv> --frames N [K]      table of N consecutive frames (kick to kick), starting at frame K (default: middle)
  tl_analyze.py <csv> --dump T0_MS D_MS   merged event list for D ms starting T0 ms after the first event (GS_IDLE hidden)

Definitions (all times in ms of host time):
  frame     = interval between D1 kicks, or guest kick-hook entries when the asynchronous worker is disabled.
              A kick or limiter entry is not proof of a new, unique world image.
  vblanks   = how many guest vblanks lie between the two kicks (1 = one frame per vblank = 120 fps at KZ_FPS=120)
  EE awake  = frame length minus the EE thread's sleeps in the scheduler (pacing sleep in processDueDeadlines + idle waits)
  worker DMA= JOB_B..JOB_E of the DMA job that the kick queued (VU1 + GIF work, includes waits for the GS thread)
  D1 done   = DONE_POST time minus kick time (the worker posted the completion event)
  applied   = EE applied the completion (STR clear, D_STAT, DMAC handler queued) minus DONE_POST
"""
import argparse, csv, statistics, bisect, collections, math


# Keep these guest-hook addresses in sync with src/kz_addrs.cpp.
REGION_HOOKS = {
    'us':  {'frame_limiter': 0x001bff10, 'kick': 0x00150090, 'kick_check': 0x00151fc8},
    'pal': {'frame_limiter': 0x001c0278, 'kick': 0x00150068, 'kick_check': 0x00151fa0},
    'eu':  {'frame_limiter': 0x001c0190, 'kick': 0x00150068, 'kick_check': 0x00151fa0},
}


def load(path):
    ev = []
    with open(path) as f:
        r = csv.reader(f)
        next(r)
        for t, lane, name, a, b, c in r:
            ev.append((float(t), lane, name, int(a), int(b), int(c)))
    if not ev:
        raise ValueError('timeline contains no events')
    ev.sort(key=lambda e: e[0])
    t0 = ev[0][0]
    return [(t - t0, l, n, a, b, c) for t, l, n, a, b, c in ev]


def select_window(ev, start_s, end_s):
    if not (math.isfinite(start_s) and math.isfinite(end_s)) or start_s < 0 or end_s <= start_s:
        raise ValueError('timeline window must have 0 <= start < end with finite times')
    if end_s * 1e6 > ev[-1][0]:
        raise ValueError('timeline ends before the requested window')
    selected = [event for event in ev if start_s * 1e6 <= event[0] < end_s * 1e6]
    if not selected:
        raise ValueError('timeline window contains no events')
    t0 = selected[0][0]
    rebased = []
    seen_idle = False
    for t, lane, name, a, b, c in selected:
        if name == 'GS_IDLE_E' and not seen_idle:
            a = 0  # Its aggregate can include idle time before the selected window.
            seen_idle = True
        rebased.append((t - t0, lane, name, a, b, c))
    return rebased


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p))] if v else float('nan')


def stat(v):
    if not v:
        return 'n=0'
    return f'n={len(v)} mean={statistics.mean(v):.2f} med={statistics.median(v):.2f} p10={pct(v,.1):.2f} p90={pct(v,.9):.2f} max={max(v):.2f}'


def spans(ev, b, e, key=lambda a, bb: 0):
    out, open_ = [], {}
    for t, lane, name, a, bb, c in ev:
        if name == b:
            open_[key(a, bb)] = (t, a, bb)
        elif name == e:
            k = key(a, bb)
            if k in open_:
                t0, a0, b0 = open_.pop(k)
                out.append((t0, t, a0, b0))
    return out


class Cover:
    """Total length of a set of spans clipped to [t0,t1]."""
    def __init__(self, sp):
        self.sp = sorted(sp)
        self.st = [s for s, _ in self.sp]

    def total(self, t0, t1):
        i = max(0, bisect.bisect_left(self.st, t0) - 1)
        tot = 0.0
        while i < len(self.sp) and self.sp[i][0] < t1:
            s, e = self.sp[i]
            tot += max(0.0, min(e, t1) - max(s, t0))
            i += 1
        return tot


def nxt(arr, t):
    i = bisect.bisect_left(arr, t)
    return arr[i] if i < len(arr) else None


def report_vblank_deadlines(ev, hz):
    period = int(1_000_000 / hz + 0.5)
    vblanks = [(t, a) for t, l, n, a, b, c in ev if n == 'VBLANK']
    if any(right[1] != left[1] + 1 for left, right in zip(vblanks, vblanks[1:])):
        raise ValueError('vblank tick IDs must be consecutive for deadline analysis')
    phase = min(t - tick * period for t, tick in vblanks)
    lateness = [(t - tick * period - phase) / 1000 for t, tick in vblanks]
    print('  fixed %d Hz vblank deadline lateness lower bound ms: median=%.2f p95=%.2f' %
          (hz, statistics.median(lateness), pct(lateness, .95)))
    wakes = [(t, b) for t, l, n, a, b, c in ev if n == 'SLEEP_E' and a == 0]
    wake_times = [t for t, b in wakes]
    long_indices = [i for i in range(1, len(vblanks)) if vblanks[i][0] - vblanks[i - 1][0] > 1.5 * period]
    close = []
    for i in long_indices:
        previous, current = vblanks[i - 1][0], vblanks[i][0]
        wake_index = bisect.bisect_right(wake_times, current) - 1
        if (wake_index >= 0 and wakes[wake_index][1] == 0 and previous < wakes[wake_index][0] and
                current - wakes[wake_index][0] <= 100):
            close.append(lateness[i])
    print('  long vblank intervals (>1.5 periods): %d; no-post pacing wake <=0.10 ms before vblank: %d' %
          (len(long_indices), len(close)))
    if close:
        print('  those long intervals: median lower-bound lateness %.2f ms, p95 %.2f ms' %
              (statistics.median(close), pct(close, .95)))
    print('  lower bounds assume one fixed host period throughout the trace; requested wait deadlines are not recorded')


def analyse(ev, region='us', vblank_hz=None):
    addresses = REGION_HOOKS[region]
    dur_us = ev[-1][0]
    if dur_us <= 0:
        raise ValueError('timeline must span a positive duration')
    dur = dur_us / 1e6
    vb = [t for t, l, n, a, b, c in ev if n == 'VBLANK']
    if len(vb) < 2:
        raise ValueError('timeline must contain at least two vblanks')
    d1_kicks = [t for t, l, n, a, b, c in ev if n == 'D1_KICK' and b & 1]
    guest_kicks = [t for t, l, n, a, b, c in ev if n == 'G_ENTER' and a == addresses['kick']]
    kt = d1_kicks if d1_kicks else guest_kicks
    kick_source = 'D1_KICK' if d1_kicks else 'guest kick hook' if guest_kicks else 'unavailable'
    print(f'window {dur:.1f} s, {len(ev)} events; vblanks {len(vb)} ({len(vb)/dur:.1f}/s)')
    print(f'guest kick entries {len(guest_kicks)} ({len(guest_kicks)/dur:.1f}/s); '
          f'worker D1_KICK events {len(d1_kicks)} ({len(d1_kicks)/dur:.1f}/s); frame breakdown source: {kick_source}')
    print('vblank interval ms:', stat([(y - x) / 1000 for x, y in zip(vb, vb[1:])]))
    if vblank_hz is not None:
        report_vblank_deadlines(ev, vblank_hz)
    print('kick delay after its vblank ms:', stat([(t - vb[bisect.bisect_right(vb, t) - 1]) / 1000 for t in kt if bisect.bisect_right(vb, t) > 0]))
    slp = spans(ev, 'SLEEP_B', 'SLEEP_E', key=lambda a, b: a)
    pace = Cover([(s, e) for s, e, a, b in slp if a == 0])
    idle = Cover([(s, e) for s, e, a, b in slp if a == 1])
    bar = spans(ev, 'BARRIER_B', 'BARRIER_E', key=lambda a, b: a)
    jobs = spans(ev, 'JOB_B', 'JOB_E')
    dma = sorted([j for j in jobs if j[2] == 1])
    gsj = [j for j in jobs if j[2] == 3]
    mscal = [j for j in jobs if j[2] == 2]
    posts = sorted(t for t, l, n, a, b, c in ev if n == 'DONE_POST' and a == 1)
    applies = sorted(s for s, e, a, b in spans(ev, 'APPLY_B', 'APPLY_E'))
    idle_seen = sorted(t for t, l, n, a, b, c in ev if n == 'CHCR_IDLE' and a == 1)
    gsv = spans(ev, 'GS_VSYNC_B', 'GS_VSYNC_E')
    gidle_e = [(t, a / 1000.0) for t, l, n, a, b, c in ev if n == 'GS_IDLE_E']  # (time of the frame start, idle ms since the previous one)
    thr = spans(ev, 'GS_THROTTLE_B', 'GS_THROTTLE_E')
    ring = spans(ev, 'GS_RING_B', 'GS_RING_E')
    dstart = [s for s, e, a, b in dma]

    print('\n-- EE thread --')
    print('  pacing sleep (processDueDeadlines): %.1f%% of the window; idle waits %.1f%%' % (100 * sum(e - s for s, e, a, b in slp if a == 0) / dur_us, 100 * sum(e - s for s, e, a, b in slp if a == 1) / dur_us))
    print('  barrier waits (worker busy when the EE touched VIF/VU state): n=%d, %.1f ms total = %.2f%% of the window' % (len(bar), sum(e - s for s, e, a, b in bar) / 1000, 100 * sum(e - s for s, e, a, b in bar) / dur_us))
    lim = sorted(t for t, l, n, a, b, c in ev if n == 'G_ENTER' and a == addresses['frame_limiter'])
    print('  frame limiter entries (0x%08x): %d (%.1f/s); period ms: %s' %
          (addresses['frame_limiter'], len(lim), len(lim) / dur, stat([(y - x) / 1000 for x, y in zip(lim, lim[1:])])))
    hooks = collections.Counter(a for t, l, n, a, b, c in ev if n == 'G_ENTER')
    print('  guest hook entries:', {hex(k): v for k, v in sorted(hooks.items())})
    # kick check at the vblank handler: b = flag | state<<8 | cur<<16
    chk = [(t, b) for t, l, n, a, b, c in ev if n == 'G_ENTER' and a == addresses['kick_check']]
    cls = collections.Counter(((b & 0xff), ((b >> 8) & 0xff)) for t, b in chk)
    print('  vblank kick check (flag, next-buffer state) -> count (kick needs flag!=0 and state==2):', dict(sorted(cls.items())))

    print('\n-- worker (VIF1/VU1/GIF) --')
    print('  DMA job ms:', stat([(e - s) / 1000 for s, e, a, b in dma]))
    print('  GS half of vblank (kz runVsync) ms:', stat([(e - s) / 1000 for s, e, a, b in gsj]))
    print('  kick -> job start ms:', stat([(nxt(dstart, t - 50) - t) / 1000 for t in kt if nxt(dstart, t - 50) is not None]))
    print('  kick -> DONE_POST ms:', stat([(nxt(posts, t) - t) / 1000 for t in kt if nxt(posts, t) is not None]))
    print('  DONE_POST -> applied on EE ms:', stat([(nxt(applies, p) - p) / 1000 for p in posts if nxt(applies, p) is not None]))
    if idle_seen:
        print('  kick -> EE poll first sees D1 idle ms:', stat([(nxt(idle_seen, t) - t) / 1000 for t in kt if nxt(idle_seen, t) is not None]))
    if jobs:
        print('  busy: DMA jobs %.1f%%, all jobs %.1f%% of the window (jobs include waits for the GS thread)' % (100 * sum(e - s for s, e, a, b in dma) / dur_us, 100 * sum(e - s for s, e, a, b in jobs) / dur_us))
    else:
        print('  worker utilization unavailable: no worker job spans were recorded')
    print('  blocked in kzgs frame throttle: n=%d, %.1f ms; ring full: n=%d, %.1f ms' % (len(thr), sum(e - s for s, e, a, b in thr) / 1000, len(ring), sum(e - s for s, e, a, b in ring) / 1000))
    print('  frames queued in kzgs after each push:', dict(collections.Counter(a for t, l, n, a, b, c in ev if n == 'GS_QUEUED')))

    print('\n-- GS thread (kzgs / PCSX2 renderer) --')
    print('  GS vsync completions: %d (%.1f/s; not necessarily unique images)' % (len(gsv), len(gsv) / dur))
    print('  GSvsync (render+present of one frame) ms:', stat([(e - s) / 1000 for s, e, a, b in gsv]))
    gi = sum(a for t, a in gidle_e) * 1000 / dur_us
    print('  idle: %.1f%% of the window => busy %.1f%% = %.2f ms per vblank (GS work is in the GIF transfers and draws, GSvsync itself is the present)' % (100 * gi, 100 * (1 - gi), (1 - gi) * dur_us / 1000 / len(vb)))

    # ---- per frame ----
    def vcount(t0, t1):
        return bisect.bisect_right(vb, t1) - bisect.bisect_right(vb, t0)

    kt = [t for t in kt if t >= vb[0]]
    frames = []
    for k in range(len(kt) - 1):
        t0, t1 = kt[k], kt[k + 1]
        n = vcount(t0, t1)
        awake = (t1 - t0) - pace.total(t0, t1) - idle.total(t0, t1)
        p = nxt(posts, t0)
        j = nxt(dstart, t0 - 50)
        je = None
        if j is not None:
            for s, e, a, b in dma:
                if s == j:
                    je = e
                    break
        frames.append(dict(t0=t0, t1=t1, n=n, len=(t1 - t0) / 1000, awake=awake / 1000, pace=pace.total(t0, t1) / 1000,
                           done=(p - t0) / 1000 if p else float('nan'), job=(je - j) / 1000 if je else float('nan'),
                           gsidle=sum(a for t, a in gidle_e if t0 <= t < t1), kickph=(t0 - vb[bisect.bisect_right(vb, t0) - 1]) / 1000))
    print('\n-- frames by length (kick to kick) --')
    bycl = collections.defaultdict(list)
    for f in frames:
        bycl[min(f['n'], 3)].append(f)
    print('  vblanks per frame -> count:', {k: len(v) for k, v in sorted(bycl.items())})
    hdr = '  %-9s %6s %8s %8s %9s %8s %8s %8s'
    print(hdr % ('class', 'n', 'len ms', 'EE awake', 'EE pacing', 'D1 done', 'job ms', 'GS idle'))
    for k, v in sorted(bycl.items()):
        def m(key):
            values = [x[key] for x in v if x[key] == x[key]]
            return '%.2f' % statistics.mean(values) if values else 'n/a'
        print(hdr % ('%d vblank%s' % (k, '' if k == 1 else 's') + ('+' if k == 3 else ''), len(v), m('len'), m('awake'), m('pace'), m('done'), m('job'), m('gsidle')))
    aw = [f['awake'] for f in frames]
    print('  EE awake time per frame ms (all frames):', stat(aw))
    vp = statistics.median([(y - x) / 1000 for x, y in zip(vb, vb[1:])])   # vblank period, ms
    over = sum(1 for f in frames if f['awake'] > vp)
    print('  frames whose EE awake time exceeds one vblank (%.2f ms): %d of %d (%.1f%%)' % (vp, over, len(frames), 100 * over / max(1, len(frames))))
    two = [f for f in frames if f['n'] >= 2]
    if posts:
        late = sum(1 for f in two if f['done'] > vp)
        print('  of the %d frames that took >= 2 vblanks: D1 completion later than one vblank after the kick in %d, EE awake time under one vblank in %d' % (len(two), late, sum(1 for f in two if f['awake'] < vp)))
    else:
        print('  D1 completion timing unavailable: no DONE_POST events were recorded')
    return frames


def print_frames(ev, n, start=None, region='us', vblank_hz=None):
    fr = analyse_quiet(ev, region, vblank_hz)
    if not fr:
        raise ValueError('timeline has fewer than two usable kick events for this region')
    if start is None:
        start = len(fr) // 2
    elif start >= len(fr):
        raise ValueError('frame start index exceeds the available kick intervals')
    print('%5s %9s %3s %7s %7s %7s %7s %7s %7s' % ('frame', 't ms', 'vb', 'len', 'awake', 'pacing', 'D1done', 'job', 'kickph'))
    def fmt(value):
        return '%7.2f' % value if value == value else '%7s' % 'n/a'
    for i, f in enumerate(fr[start:start + n], start):
        print('%5d %9.1f %3d %s %s %s %s %s %s' %
              (i, f['t0'] / 1000, f['n'], *(fmt(f[key]) for key in ('len', 'awake', 'pace', 'done', 'job', 'kickph'))))


def analyse_quiet(ev, region='us', vblank_hz=None):
    import io, contextlib
    with contextlib.redirect_stdout(io.StringIO()):
        return analyse(ev, region, vblank_hz)


def self_test():
    import contextlib, io, re
    from pathlib import Path

    source = (Path(__file__).resolve().parents[2] / 'src' / 'kz_addrs.cpp').read_text(encoding='utf-8')
    for region, suffix in (('us', 'Us'), ('pal', 'Pal'), ('eu', 'Eu')):
        table = re.search(r'const KzAddrs kKzAddrs' + suffix + r' = \{(.*?)\n\};', source, re.S)
        assert table, suffix
        for field, key in (('frameLimiter', 'frame_limiter'), ('kick', 'kick'), ('kickCheck', 'kick_check')):
            value = re.search(r'\.' + field + r'\s*=\s*(0x[0-9a-fA-F]+)', table.group(1))
            assert value and int(value.group(1), 16) == REGION_HOOKS[region][key], (region, field)

    addresses = REGION_HOOKS['eu']
    ev = []
    for t in (0, 16000, 32000):
        ev.extend([
            (t, 'EE', 'VBLANK', 0, 0, 0),
            (t + 100, 'EE', 'G_ENTER', addresses['kick'], 0, 0),
            (t + 200, 'EE', 'G_ENTER', addresses['frame_limiter'], 0, 0),
            (t + 300, 'GS', 'GS_VSYNC_B', 0, 0, 0),
            (t + 800, 'GS', 'GS_VSYNC_E', 0, 0, 0),
        ])
    with contextlib.redirect_stdout(io.StringIO()) as output:
        frames = analyse(ev, 'eu')
    report = output.getvalue()
    assert len(frames) == 2 and all(frame['n'] == 1 for frame in frames)
    assert 'frame breakdown source: guest kick hook' in report
    assert 'frame limiter entries (0x001c0190): 3' in report
    assert 'worker utilization unavailable' in report and 'D1 completion timing unavailable' in report
    assert 'GS vsync completions: 3' in report

    async_events = list(ev)
    for t in (0, 16000, 32000):
        async_events.extend([
            (t + 110, 'EE', 'D1_KICK', 0, 1, 0),
            (t + 150, 'worker', 'JOB_B', 1, 0, 0),
            (t + 400, 'worker', 'JOB_E', 1, 0, 0),
            (t + 450, 'worker', 'DONE_POST', 1, 0, 0),
        ])
    async_events.sort(key=lambda event: event[0])
    with contextlib.redirect_stdout(io.StringIO()) as output:
        async_frames = analyse(async_events, 'eu')
    async_report = output.getvalue()
    assert len(async_frames) == 2 and 'frame breakdown source: D1_KICK' in async_report
    assert 'worker D1_KICK events 3' in async_report and 'DMA job ms: n=3' in async_report
    assert 'kick -> DONE_POST ms: n=3' in async_report and 'busy: DMA jobs' in async_report
    assert 'D1 completion timing unavailable' not in async_report

    delayed = [
        (0, 'EE', 'VBLANK', 1, 0, 0),
        (15000, 'EE', 'SLEEP_B', 0, 0, 0),
        (31663, 'EE', 'SLEEP_E', 0, 0, 0),
        (31667, 'EE', 'VBLANK', 2, 0, 0),
        (33334, 'EE', 'VBLANK', 3, 0, 0),
        (58334.5, 'EE', 'VBLANK', 4, 0, 0),
        (83235, 'EE', 'SLEEP_E', 0, 0, 0),
        (83334.9, 'EE', 'SLEEP_E', 0, 0, 0),
        (83335, 'EE', 'SLEEP_E', 0, 1, 0),
        (83335.1, 'EE', 'VBLANK', 5, 0, 0),
    ]
    with contextlib.redirect_stdout(io.StringIO()) as output:
        report_vblank_deadlines(delayed, 60)
    assert 'long vblank intervals (>1.5 periods): 2' in output.getvalue()
    assert 'no-post pacing wake <=0.10 ms before vblank: 1' in output.getvalue()
    assert 'median lower-bound lateness 15.00 ms' in output.getvalue()
    for gap, expected in ((100, 1), (100.1, 0)):
        threshold = [
            (0, 'EE', 'VBLANK', 1, 0, 0),
            (31667 - gap, 'EE', 'SLEEP_E', 0, 0, 0),
            (31667, 'EE', 'VBLANK', 2, 0, 0),
        ]
        with contextlib.redirect_stdout(io.StringIO()) as output:
            report_vblank_deadlines(threshold, 60)
        assert 'no-post pacing wake <=0.10 ms before vblank: %d' % expected in output.getvalue()
    try:
        report_vblank_deadlines([(0, 'EE', 'VBLANK', 1, 0, 0), (1, 'EE', 'VBLANK', 3, 0, 0)], 60)
    except ValueError:
        pass
    else:
        raise AssertionError('missing vblank tick was accepted')
    cropped = select_window(delayed, 0.015, 0.04)
    assert len(cropped) == 4 and cropped[0][0] == 0 and cropped[-1][0] == 18334
    assert len(select_window(delayed, 0, 0.015)) == 1
    idle_events = [(0, 'GS', 'GS_IDLE_E', 9000, 0, 0), (15000, 'GS', 'GS_IDLE_E', 3000, 0, 0),
                   (20000, 'EE', 'VBLANK', 1, 0, 0)]
    assert [event[3] for event in select_window(idle_events, 0, 0.019)] == [0, 3000]
    for start, end in ((0, 0), (float('nan'), 1), (1, 2)):
        try:
            select_window(delayed, start, end)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid timeline window was accepted')
    print('timeline analyzer self-test passed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('csv', nargs='?', help='KZ_TIMELINE CSV file')
    parser.add_argument('--region', choices=REGION_HOOKS, default='us', help='disc executable region (default: us)')
    parser.add_argument('--vblank-hz', type=int, help='fixed guest rate for deadline-lateness lower bounds (30-480)')
    parser.add_argument('--window-s', nargs=2, type=float, metavar=('START', 'END'),
                        help='analyze [start, end) seconds after the first recorded event')
    group = parser.add_mutually_exclusive_group()
    group.add_argument('--dump', nargs=2, type=float, metavar=('T0_MS', 'DURATION_MS'))
    group.add_argument('--frames', nargs='+', type=int, metavar='N_OR_START')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
    elif not args.csv:
        parser.error('a timeline CSV file is required')
    elif args.frames and (len(args.frames) > 2 or args.frames[0] <= 0 or
                          (len(args.frames) == 2 and args.frames[1] < 0)):
        parser.error('--frames needs a positive count and an optional non-negative start index')
    elif args.vblank_hz is not None and not 30 <= args.vblank_hz <= 480:
        parser.error('--vblank-hz must be between 30 and 480')
    elif args.dump and args.dump[1] <= 0:
        parser.error('--dump duration must be positive')
    else:
        try:
            ev = load(args.csv)
            if args.window_s:
                ev = select_window(ev, *args.window_s)
            if args.dump:
                s, d = (value * 1000 for value in args.dump)
                for t, l, n, a, b, c in ev:
                    if s <= t < s + d and n != 'GS_IDLE_E':
                        print(f'{t/1000:10.3f} ms {l:6s} {n:14s} a={a:#x} b={b:#x} c={c}')
            elif args.frames:
                print_frames(ev, args.frames[0], args.frames[1] if len(args.frames) == 2 else None, args.region, args.vblank_hz)
            else:
                analyse(ev, args.region, args.vblank_hz)
        except (OSError, ValueError) as error:
            parser.error(str(error))
