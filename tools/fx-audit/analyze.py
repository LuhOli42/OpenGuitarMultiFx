#!/usr/bin/env python3
"""Audio metrics for the FX audit: levels, EQ signature, THD, echoes."""
import numpy as np, soundfile as sf, json, os, re
from scipy import signal

A = os.environ.get('FX_AUDIT_DIR', '/tmp/fx-audit')
OUT = A + '/out'; PRB = A + '/probes'
SR = 44100

def load(path):
    d, sr = sf.read(path)
    if d.ndim > 1: d = d.mean(axis=1)
    return d.astype(np.float64), sr

def bands(x, sr, edges=(20,120,400,800,1600,3200,8000,20000)):
    f, P = signal.welch(x, sr, nperseg=8192)
    out = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = (f >= lo) & (f < hi)
        out.append(10*np.log10(P[m].sum()/P.sum() + 1e-20))
    return np.array(out)

def band_levels(x, sr, edges=(20,120,400,800,1600,3200,8000,20000)):
    f, P = signal.welch(x, sr, nperseg=8192)
    out = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = (f >= lo) & (f < hi)
        out.append(10*np.log10(P[m].mean() + 1e-20))
    return np.array(out)

def basic(x, sr):
    peak = float(np.abs(x).max())
    rms = float(np.sqrt((x**2).mean()))
    nz = x[np.abs(x) > 0.001]
    crest = peak/(np.sqrt((nz**2).mean())+1e-12) if len(nz) else 0.
    return dict(peak=peak, rms=rms, rms_db=20*np.log10(rms+1e-12),
                crest=crest, dc=float(x.mean()),
                clip_frac=float((np.abs(x) > 0.98).mean()),
                nan=int(np.isnan(x).sum()),
                silent=bool(rms < 1e-4))

def thd(x, sr, f0):
    n = len(x)
    w = signal.windows.hann(n)
    X = np.abs(np.fft.rfft(x*w))
    freqs = np.fft.rfftfreq(n, 1/sr)
    def amp_at(f):
        i = np.argmin(np.abs(freqs - f))
        return X[max(0,i-2):i+3].max()
    fund = amp_at(f0)
    harms = {h: amp_at(h*f0) for h in range(2, 9)}
    total_h = np.sqrt(sum(v**2 for v in harms.values()))
    even = np.sqrt(sum(harms[h]**2 for h in (2,4,6,8)))
    odd = np.sqrt(sum(harms[h]**2 for h in (3,5,7)))
    # noise: rms of residual outside +-30Hz of harmonics
    X2 = X.copy()
    for h in range(1, 12):
        i = np.argmin(np.abs(freqs - h*f0))
        X2[max(0,i-int(30* n/sr)):i+int(30*n/sr)] = 0
    return dict(fund_amp=float(fund), thd=float(total_h/fund) if fund else 0.,
                thd_pct=float(100*total_h/fund) if fund else 0.,
                even_odd=float(even/(odd+1e-12)),
                h2_rel=float(harms[2]/fund) if fund else 0.,
                h3_rel=float(harms[3]/fund) if fund else 0.)

def freq_response(wet, dry, sr):
    f, Pxx = signal.welch(dry, sr, nperseg=16384)
    f, Pxy = signal.csd(dry, wet, sr, nperseg=16384)
    H = np.abs(Pxy)/ (Pxx + 1e-20)
    pts = [80,150,300,500,720,1000,1500,2500,4000,6000,9000,12000]
    return {str(p): float(20*np.log10(H[np.argmin(np.abs(f-p))]+1e-12)) for p in pts}

def echoes(x, sr):
    env = np.abs(signal.hilbert(x))
    env = signal.savgol_filter(env, 501, 2)
    # clicks are 0.5s apart -> normalize envelope, autocorrelate to find repeats
    e = env[:int(3.5*sr)]
    e = e - e.mean()
    ac = np.correlate(e, e, 'full')[len(e)-1:]
    ac /= (ac[0] + 1e-12)
    peaks, _ = signal.find_peaks(ac[int(0.05*sr):], height=0.05, distance=int(0.04*sr))
    lags = (peaks + int(0.05*sr))/sr
    # tail level between clicks
    seg = env[int(1.6*sr):int(2.0*sr)]
    return dict(n_echo=int(len(peaks)), echo_lags=[round(float(l),3) for l in lags[:8]],
                echo_strengths=[round(float(ac[p+int(0.05*sr)]),3) for p in peaks[:8]],
                tail_rms=float(20*np.log10(np.sqrt((seg**2).mean())+1e-12)))

di, sr = load(os.environ.get('DI_WAV', 'Tests/fixtures/di/guitar-di.wav'))
di_band = band_levels(di, sr)
di_basic = basic(di, sr)
res = {'DI': {**di_basic, 'band_db': band_levels(di, sr).tolist()}}

for fn in sorted(os.listdir(OUT)):
    if not fn.endswith('.wav'): continue
    name = fn[:-4]
    x, s = load(f'{OUT}/{fn}')
    b = basic(x, s)
    b['band_delta_db'] = (band_levels(x, s) - di_band).tolist()
    res[name] = b

prb_sweep_dry, _ = load(os.environ.get('PROBE_WAV_DIR', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'probes')) + '/probe-sweep.wav')
prb = {}
for fn in sorted(os.listdir(PRB)):
    if not fn.endswith('.wav'): continue
    name = fn[:-4]
    x, s = load(f'{PRB}/{fn}')
    b = basic(x, s)
    if 'sine220' in name: b.update(thd(x, s, 220.))
    if 'sine110' in name: b.update(thd(x, s, 110.))
    if 'sweep' in name: b['fr_db'] = freq_response(x, prb_sweep_dry, s)
    if 'clicks' in name: b.update(echoes(x, s))
    prb[name] = b

json.dump({'music': res, 'probes': prb}, open(A + '/metrics.json', 'w'), indent=1)
print('analyzed', len(res)-1, 'music renders,', len(prb), 'probes')
