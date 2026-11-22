#!/usr/bin/env python3
"""audiocmp.py: does a recording play the same notes as a reference?

usage: python3 port/tools/audiocmp.py PORT.wav REF.wav     (needs numpy: the system python3 has it)
prints SAME / DIFF  corr C  lag L s  frames N  (exit 0 for SAME)

Both are cut into 80 ms windows every 20 ms, each window summed into 60 semitone bands (A2-A7) and
log-scaled, so the comparison is of notes and their timing, not of samples: the port's OPL emulator
(Nuked) and DOSBox-X's differ in level and timbre detail, and the two runs start at different times.
Each band's mean over the recording is removed first; without that the spectrum's overall slope
dominates and white noise scored 0.912. The reference's loud frames (over a fifth of its loudest) are
matched against the port's at the best shift within +-4 s; SAME when the mean correlation is at least
0.9. Measured on the Apogee song (captures/apogee_ww.wav): the port 0.956 (0.946 before it played
the logo's voice), the port writing each OPL value one register up 0.847, reading the 16-bit effects
as signed 0.838, the menu song 0.051, noise 0.013.
"""
import sys
import wave

import numpy as np

HOP, WIN, MAXLAG, LIMIT = 0.02, 0.08, 200, 0.9


def load(path):
    w = wave.open(path)
    a = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, w.getnchannels())
    return a.mean(axis=1), w.getframerate()


def bands(a, rate):
    n, h = int(WIN * rate), int(HOP * rate)
    win, fr = np.hanning(n), np.fft.rfftfreq(n, 1 / rate)
    edges = 110 * 2 ** (np.arange(61) / 12)
    masks = [(fr >= edges[k]) & (fr < edges[k + 1]) for k in range(60)]
    rows = []
    for i in range(0, len(a) - n, h):
        p = np.abs(np.fft.rfft(a[i:i + n] * win)) ** 2
        rows.append([p[m].sum() for m in masks])
    return np.log1p(np.array(rows))


def unit(x):
    x = x - x.mean(axis=0, keepdims=True)          # each band's own level over the run: the timbre
    x = x - x.mean(axis=1, keepdims=True)
    d = np.linalg.norm(x, axis=1, keepdims=True)
    d[d == 0] = 1
    return x / d


def main(port, ref):
    sp, sr = bands(*load(port)), bands(*load(ref))
    up, ur = unit(sp), unit(sr)
    loud = sr.sum(axis=1)
    active = np.where(loud > loud.max() * 0.2)[0]
    best = (-1.0, 0, 0)
    for lag in range(-MAXLAG, MAXLAG + 1):
        idx = active[(active + lag >= 0) & (active + lag < len(up))]
        if len(idx) < len(active) // 2:
            continue
        c = float(np.mean(np.sum(ur[idx] * up[idx + lag], axis=1)))
        if c > best[0]:
            best = (c, lag, len(idx))
    c, lag, n = best
    verdict = 'SAME' if c >= LIMIT else 'DIFF'
    print('%s  corr %.3f  lag %+.2f s  frames %d' % (verdict, c, lag * HOP, n))
    return 0 if verdict == 'SAME' else 1


if __name__ == '__main__':
    sys.exit(main(*sys.argv[1:3]))
