#!/usr/bin/env python3
"""Drive-swept probe: sweep each variant's main gain control and measure per
setting, so circuit differences hidden by a single fixed-drive point show up.

Why this exists: the fixed 0.85-drive hot probe measured TS808/TS9/TS10 and
RAT/RAT2/TurboRAT "nearly identical" (see PR #17). At deep clipping the
circuits genuinely converge and the registry's unity trims normalize the
internal level differences on top — a single-point probe is blind to clip-knee
and headroom differences that only appear across the control's travel.

For each variant we render at DRIVE_POINTS (default 0.1..0.9) two inputs:
  - the guitar DI          -> band_delta_db + rms_db (EQ/level signature)
  - probe-sine220.wav      -> thd_pct + h2/h3 + even/odd (clip-knee signature)

Usage:
  FX_AUDIT_DIR=/tmp/fx-audit python3 tools/fx-audit/run_drive_sweep.py
  python3 tools/fx-audit/run_drive_sweep.py --update-metrics=tools/fx-audit/metrics.json

Env overrides: RENDERFX_BIN, FX_AUDIT_DIR, DI_WAV, PROBE_WAV_DIR,
DRIVE_POINTS ("0.1 0.3 0.5 0.7 0.9"), SWEEP_KEYS (subset of keys/families).
"""
import subprocess, os, sys, json, concurrent.futures
import numpy as np, soundfile as sf
from scipy import signal

BIN = os.environ.get('RENDERFX_BIN', 'build/Tests/OpenGuitarMultiFx_RenderFx_artefacts/Release/OpenGuitarMultiFx_RenderFx')
A = os.environ.get('FX_AUDIT_DIR', '/tmp/fx-audit')
DI = os.environ.get('DI_WAV', 'Tests/fixtures/di/guitar-di.wav')
PROBES_DIR = os.environ.get('PROBE_WAV_DIR', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'probes'))
SINE = PROBES_DIR + '/probe-sine220.wav'
OUT = A + '/drive-sweep'
os.makedirs(OUT, exist_ok=True)

DRIVES = [float(v) for v in os.environ.get('DRIVE_POINTS', '0.1 0.3 0.5 0.7 0.9').split()]

CAVEAT = ("Fixed-drive single-point probes can hide real circuit differences: "
          "variants converge at deep clipping and the registry's unity trims "
          "normalize internal level differences away. A drive-swept probe is "
          "the discriminating tool (PR #17).")

# variant families: name -> [(effect key, main drive param id)]
FAMILIES = {
    'ts': [('TS808StyleOverdrive', 'ts808_drive'),
           ('TS9StyleOverdrive', 'ts9_drive'),
           ('TS10StyleOverdrive', 'ts10_drive')],
    'rat': [('RatStyleDistortion', 'rat_distortion'),
            ('RAT2StyleDistortion', 'rat_distortion'),
            ('TurboRatStyleDistortion', 'rat_distortion')],
    'muff': [('BigMuffStyleFuzz', 'bmp_sustain'),
             ('SovtekBigMuffStyleFuzz', 'bmpsv_sustain'),
             ('RussianBigMuffStyleFuzz', 'bmpru_sustain')],
    'boss': [('DS1StyleDistortion', 'ds1_drive'),
             ('MetalZoneStyleDistortion', 'mt2_dist'),
             ('HM2StyleDistortion', 'hm2_dist')],
}

only = os.environ.get('SWEEP_KEYS')
if only:
    want = set(only.split())
    FAMILIES = {f: [(k, p) for k, p in fam if k in want or f in want]
                for f, fam in FAMILIES.items()}
    FAMILIES = {f: fam for f, fam in FAMILIES.items() if fam}

def load(path):
    d, sr = sf.read(path)
    if d.ndim > 1: d = d.mean(axis=1)
    return d.astype(np.float64), sr

def band_levels(x, sr, edges=(20,120,400,800,1600,3200,8000,20000)):
    f, P = signal.welch(x, sr, nperseg=8192)
    out = []
    for lo, hi in zip(edges[:-1], edges[1:]):
        m = (f >= lo) & (f < hi)
        out.append(10*np.log10(P[m].mean() + 1e-20))
    return np.array(out)

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
    return dict(thd_pct=float(100*total_h/fund) if fund else 0.,
                even_odd=float(even/(odd+1e-12)),
                h2_rel=float(harms[2]/fund) if fund else 0.,
                h3_rel=float(harms[3]/fund) if fund else 0.)

# --- render pass ---------------------------------------------------------
jobs = []
for fam, variants in FAMILIES.items():
    for key, param in variants:
        for d in DRIVES:
            tag = f'{key}__{param}={d:g}'
            jobs.append((f'{tag}__di', DI, key, [f'{param}={d:g}']))
            jobs.append((f'{tag}__sine220', SINE, key, [f'{param}={d:g}']))

def run(job):
    outname, inf, key, extra = job
    out = f'{OUT}/{outname}.wav'
    r = subprocess.run([BIN, inf, out, key] + extra,
                       capture_output=True, text=True, timeout=1200)
    return outname, r.stdout.strip(), r.stderr.strip()[-200:], r.returncode

fails = []
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as ex:
    for i, res in enumerate(ex.map(run, jobs)):
        if res[3] != 0 or res[2]:
            fails.append(res)
            print(res[0], '| rc', res[3], '|', res[2][:150], flush=True)
if fails:
    sys.exit(f'{len(fails)} renders failed')

# --- measure pass --------------------------------------------------------
di, sr = load(DI)
di_band = band_levels(di, sr)

results = {}
for fam, variants in FAMILIES.items():
    for key, param in variants:
        settings = {}
        for d in DRIVES:
            tag = f'{key}__{param}={d:g}'
            x_di, s1 = load(f'{OUT}/{tag}__di.wav')
            x_sn, s2 = load(f'{OUT}/{tag}__sine220.wav')
            rms = float(np.sqrt((x_di**2).mean()))
            m = thd(x_sn, s2, 220.)
            m['rms_db'] = float(20*np.log10(rms+1e-12))
            m['peak'] = float(np.abs(x_di).max())
            m['band_delta_db'] = (band_levels(x_di, s1) - di_band).tolist()
            settings[f'{d:g}'] = m
        results[key] = {'param': param, 'settings': settings}

# --- divergence summary ----------------------------------------------------
def pairwise_spread(vals):
    vals = list(vals)
    return max(abs(a - b) for i, a in enumerate(vals) for b in vals[i+1:]) if len(vals) > 1 else 0.

divergence = {}
for fam, variants in FAMILIES.items():
    keys = [k for k, _ in variants]
    best_thd = {'spread': 0., 'drive': None}
    best_band = {'spread': 0., 'drive': None}
    for d in DRIVES:
        s = pairwise_spread(results[k]['settings'][f'{d:g}']['thd_pct'] for k in keys)
        if s > best_thd['spread']: best_thd = {'spread': s, 'drive': d}
        mats = [np.array(results[k]['settings'][f'{d:g}']['band_delta_db']) for k in keys]
        sb = max(float(np.abs(a - b).max())
                 for i, a in enumerate(mats) for b in mats[i+1:]) if len(mats) > 1 else 0.
        if sb > best_band['spread']: best_band = {'spread': sb, 'drive': d}
    divergence[fam] = {'max_thd_spread_pct': round(best_thd['spread'], 3),
                       'thd_spread_at_drive': best_thd['drive'],
                       'max_band_spread_db': round(best_band['spread'], 3),
                       'band_spread_at_drive': best_band['drive'],
                       'variants': keys}

sweep = {'_caveat': CAVEAT,
         'drives': DRIVES,
         'inputs': {'bands': os.path.basename(DI), 'thd': 'probe-sine220.wav'},
         'results': results,
         'divergence': divergence}

# --- report ----------------------------------------------------------------
def short(k):
    return (k.replace('StyleOverdrive', '').replace('StyleDistortion', '')
             .replace('StyleFuzz', '').replace('StyleAmplifier', ''))

for fam, variants in FAMILIES.items():
    keys = [k for k, _ in variants]
    print(f'\n=== {fam} ===  THD% (probe-sine220)')
    print('drive |' + '|'.join(f'{short(k):>12}' for k in keys) + '| spread')
    for d in DRIVES:
        thds = [results[k]['settings'][f'{d:g}']['thd_pct'] for k in keys]
        print(f'{d:5.2f} |' + '|'.join(f'{t:12.3f}' for t in thds)
              + f'| {pairwise_spread(thds):7.3f}')
    print('      max |band_delta_db| spread between variants (dB)')
    print('drive |' + '|'.join(f'{short(k):>12}' for k in keys[1:]) + ' (vs ' + short(keys[0]) + ')')
    for d in DRIVES:
        base = np.array(results[keys[0]]['settings'][f'{d:g}']['band_delta_db'])
        sp = [float(np.abs(np.array(results[k]['settings'][f'{d:g}']['band_delta_db']) - base).max())
              for k in keys[1:]]
        print(f'{d:5.2f} |' + '|'.join(f'{v:12.3f}' for v in sp))

dv = divergence
print('\n=== divergence summary ===')
for fam, dvr in dv.items():
    print(f"{fam:6} THD spread {dvr['max_thd_spread_pct']:7.3f}% @drive {dvr['thd_spread_at_drive']:g}"
          f" | band spread {dvr['max_band_spread_db']:6.2f} dB @drive {dvr['band_spread_at_drive']:g}")

json_path = A + '/drive_sweep.json'
json.dump(sweep, open(json_path, 'w'), indent=1)
print('\nwrote', json_path)

# optional: merge into a metrics.json alongside 'music'/'probes'
mp = next((a.split('=', 1)[1] for a in sys.argv[1:] if a.startswith('--update-metrics=')), None)
if mp:
    m = json.load(open(mp)) if os.path.exists(mp) else {}
    m['drive_sweep'] = sweep
    json.dump(m, open(mp, 'w'), indent=1)
    print('merged drive_sweep into', mp)
