#!/usr/bin/env python3
"""Focused fx-audit measurement for a subset of units: renders the DI + the
sine110/sine220/sweep probes through each unit and prints the audit metrics
(band delta, THD, even:odd, sweep-FR). Usage:

    measure_units.py 'Key:param=val,param=val' [...]

Each argument is one variant, e.g.
    measure_units.py 'EPStyleBooster:' 'EPStyleBooster:epbooster_level=0.9' \
        'BassmanStyleAmplifier:bm_vol_normal=0.9 bm_vol_bright=0.2'
"""
import subprocess, os, sys, json
import numpy as np, soundfile as sf
from scipy import signal

ROOT = os.path.dirname(os.path.abspath(__file__)) + '/../..'
BIN = os.environ.get('RENDERFX_BIN', ROOT + '/build/Tests/OpenGuitarMultiFx_RenderFx_artefacts/Release/OpenGuitarMultiFx_RenderFx')
PROBES = ROOT + '/tools/fx-audit/probes'
DI = os.environ.get('DI_WAV', ROOT + '/Tests/fixtures/di/guitar-di.wav')
WORK = os.environ.get('MEASURE_DIR', '/tmp/measure-units')
os.makedirs(WORK, exist_ok=True)

def load(p):
    d, sr = sf.read(p)
    if d.ndim > 1: d = d.mean(axis=1)
    return d.astype(np.float64), sr

def band_levels(x, sr, edges=(20,120,400,800,1600,3200,8000,20000)):
    f, P = signal.welch(x, sr, nperseg=8192)
    return np.array([10*np.log10(P[(f>=lo)&(f<hi)].mean()+1e-20) for lo,hi in zip(edges[:-1],edges[1:])])

def basic(x):
    return dict(rms_db=20*np.log10(np.sqrt((x**2).mean())+1e-12),
                peak=float(np.abs(x).max()),
                clip_frac=float((np.abs(x)>0.98).mean()))

def thd(x, sr, f0):
    n=len(x); w=signal.windows.hann(n); X=np.abs(np.fft.rfft(x*w)); freqs=np.fft.rfftfreq(n,1/sr)
    def amp(f):
        i=np.argmin(np.abs(freqs-f)); return X[max(0,i-2):i+3].max()
    fund=amp(f0); harms={h:amp(h*f0) for h in range(2,9)}
    even=np.sqrt(sum(harms[h]**2 for h in (2,4,6,8))); odd=np.sqrt(sum(harms[h]**2 for h in (3,5,7)))
    return dict(thd_pct=100*np.sqrt(sum(v*v for v in harms.values()))/fund if fund else 0,
                even_odd=even/(odd+1e-12))

def freq_response(wet, dry, sr):
    f,Pxx=signal.welch(dry,sr,nperseg=16384); f,Pxy=signal.csd(dry,wet,sr,nperseg=16384)
    H=np.abs(Pxy)/(Pxx+1e-20)
    pts=[80,150,300,500,720,1000,1500,2500,4000,6000,9000,12000]
    return {str(p): 20*np.log10(H[np.argmin(np.abs(f-p))]+1e-12) for p in pts}

def render(infile, key, params, tag):
    out = f'{WORK}/{tag}.wav'
    cmd = [BIN, infile, out, key] + ([p for p in params.split() if p] if params else [])
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=1200)
    if r.returncode != 0:
        print('RENDER FAIL', tag, r.stderr[-300:]); sys.exit(1)
    return out

di, di_sr = load(DI); di_band = band_levels(di, di_sr)
sweep_dry, _ = load(PROBES + '/probe-sweep.wav')

for spec in sys.argv[1:]:
    key, _, params = spec.partition(':')
    tag = key + ('-' + params.replace(' ','_').replace('=','') if params else '')
    # music render
    x, s = load(render(DI, key, params, tag+'__di'))
    b = basic(x)
    bd = band_levels(x, s) - di_band
    print(f'\n=== {tag} ===')
    print(f'  di rms_db {b["rms_db"]:+.2f} peak {b["peak"]:.3f} clip {b["clip_frac"]:.4f}')
    print('  band_delta_db: ' + ' '.join(f'{v:+.1f}' for v in bd) + '   [8-20k last]')
    # probes
    for probe in ('probe-sine110', 'probe-sine220', 'probe-sweep'):
        x, s = load(render(f'{PROBES}/{probe}.wav', key, params, tag+'__'+probe))
        bb = basic(x)
        line = f'  {probe}: rms {bb["rms_db"]:+.2f} dB'
        if 'sine' in probe:
            t = thd(x, s, 110. if '110' in probe else 220.)
            line += f' | THD {t["thd_pct"]:.2f}% | even:odd {t["even_odd"]:.3f}'
        else:
            fr = freq_response(x, sweep_dry, s)
            line += ' | FR ' + ' '.join(f'{p}Hz:{v:+.1f}' for p,v in fr.items())
        print(line)
