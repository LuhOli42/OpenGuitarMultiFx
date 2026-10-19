#!/usr/bin/env python3
"""Delta tables between two fx-audit runs (metrics.json + cpu-bench.csv).

    python3 tools/fx-audit/delta_report.py OLD_metrics.json NEW_metrics.json \
        OLD_cpu-bench.csv NEW_cpu-bench.csv > delta.md

CPU_CONTROLS (env, comma-separated keys whose code did not change between the
two runs) turns on box-speed normalisation: absolute cpu% is not portable
across VMs, so the median new/old eco ratio over the controls is printed and
every prev->now CPU comparison also gets a normalised column (now / ratio).

Prints markdown sections: coverage, CPU (eco budget movers, top offenders),
dead/silent/broken renders, unity drift, THD/band shifts, drive-sweep spread
and inert gain controls. Pure measurement -- it never edits anything.
"""
import json, csv, sys, os, statistics

FLAG = 10.0          # eco CPU budget (% of one core), same as run_cpu_bench.py
UNITY_TOL = 1.0      # dB: default render vs DI rms
CPU_MOVE = 1.5       # pp: smaller CPU changes are noise
THD_MOVE = 25.0      # % relative THD change worth listing
BAND_MOVE = 3.0      # dB: any band-delta shift worth listing

old_m, new_m = (json.load(open(p)) for p in sys.argv[1:3])
def load_cpu(p):
    d = {}
    for r in csv.DictReader(open(p)):
        d.setdefault(r['effect'], {})[r['quality']] = r
    return d
old_c, new_c = load_cpu(sys.argv[3]), load_cpu(sys.argv[4])

def eco(c, k):
    r = c.get(k, {}).get('reduced')
    return float(r['cpu_pct']) if r and r['cpu_pct'] else None
def f1(x): return '-' if x is None else f'{x:.1f}'

out = []
P = out.append
om, nm = old_m['music'], new_m['music']
op, np_ = old_m['probes'], new_m['probes']
base = lambda name: name.split('-')[0]
okeys = sorted(old_c); nkeys = sorted(new_c)
added = [k for k in nkeys if k not in old_c]
missing = [k for k in okeys if k not in new_c]
P('## Coverage\n')
P(f'- registered effects (bench rows): {len(okeys)} -> {len(nkeys)} ({len(added)} new, '
  f'{len(missing)} missing from the new bench -- timed out or unregistered)')
if missing:
    P('- missing bench rows: ' + ', '.join(f'`{k}` (prev eco {f1(eco(old_c, k))})' for k in missing))
P(f'- DI renders: {len(om)-1} -> {len(nm)-1}; probe renders: {len(op)} -> {len(np_)}\n')

# ---- CPU ----------------------------------------------------------------
P('## CPU (eco / `reduced` tier, % of one core)\n')
controls = [k for k in os.environ.get('CPU_CONTROLS', '').split(',') if k and eco(old_c, k) and eco(new_c, k)]
ratio = statistics.median(eco(new_c, k) / eco(old_c, k) for k in controls) if controls else 1.0
def norm(x): return None if x is None else x / ratio
if controls:
    P(f'- box speed: median new/old eco ratio over {len(controls)} unchanged-code controls = **{ratio:.2f}** '
      f'({", ".join(controls)}); `norm` = now / {ratio:.2f}, i.e. the new number on the old box')
of = {k for k in okeys if (eco(old_c, k) or 0) > FLAG}
nf = {k for k in nkeys if (eco(new_c, k) or 0) > FLAG}
P(f'- over the {FLAG:.0f}% budget: **{len(of)} -> {len(nf)}** (of {len(okeys)} -> {len(nkeys)})'
  + (f'; box-normalised: {len([k for k in nkeys if (norm(eco(new_c, k)) or 0) > FLAG])}' if controls else ''))
cleared = sorted(of - nf, key=lambda k: -(eco(old_c, k) or 0))
newly = sorted(k for k in nf - of if k in old_c)
newkeys_flag = sorted((k for k in nf if k not in old_c), key=lambda k: -eco(new_c, k))
P(f'- moved under budget ({len(cleared)}): ' + (', '.join(f'`{k}` {f1(eco(old_c,k))}->{f1(eco(new_c,k))}'
  + (f' (norm {f1(norm(eco(new_c,k)))})' if controls else '') for k in cleared) or 'none'))
if controls:
    ncleared = sorted((k for k in of if (norm(eco(new_c, k)) or 0) <= FLAG), key=lambda k: -(eco(old_c, k) or 0))
    nstill = sorted((k for k in of if (norm(eco(new_c, k)) or 0) > FLAG), key=lambda k: -(norm(eco(new_c, k)) or 0))
    P(f'- moved under budget, box-normalised ({len(ncleared)}): ' + (', '.join(f'`{k}` {f1(eco(old_c,k))}->{f1(norm(eco(new_c,k)))}' for k in ncleared) or 'none'))
    P(f'- still over budget, box-normalised ({len(nstill)}): ' + (', '.join(f'`{k}` {f1(eco(old_c,k))}->{f1(norm(eco(new_c,k)))}' for k in nstill) or 'none'))
P(f'- newly over budget, existing effects ({len(newly)}): ' + (', '.join(f'`{k}` {f1(eco(old_c,k))}->{f1(eco(new_c,k))}' for k in newly) or 'none'))
P(f'- new effects over budget ({len(newkeys_flag)}): ' + (', '.join(f'`{k}` {f1(eco(new_c,k))}' for k in newkeys_flag) or 'none'))
med = sorted(eco(new_c, k) or 0 for k in nkeys)
P(f'- median effect: {med[len(med)//2]:.1f}%\n')
P('### Top-10 CPU offenders (eco)\n')
P('| # | effect | eco % | norm | prev eco % | high % | peak block eco (ms) | blocks over budget (eco) |')
P('|---|---|---|---|---|---|---|---|')
top = sorted(nkeys, key=lambda k: -(eco(new_c, k) or 0))[:10]
for i, k in enumerate(top, 1):
    h = new_c[k].get('high'); r = new_c[k].get('reduced')
    P(f'| {i} | `{k}` | {f1(eco(new_c,k))} | {f1(norm(eco(new_c,k)))} | {f1(eco(old_c,k))} | {h["cpu_pct"] if h else "-"} '
      f'| {r["peak_block_ms"] if r else "-"} | {r["blocks_over_budget"] if r else "-"} |')
P('')
movers = [(k, eco(old_c, k), eco(new_c, k)) for k in okeys if k in new_c]
movers = [m for m in movers if m[1] is not None and m[2] is not None and abs(norm(m[2])-m[1]) >= CPU_MOVE]
P(f'### Eco CPU movers, existing effects (|norm - prev| >= {CPU_MOVE} pp)\n')
P('| effect | prev | now | norm | norm delta |'); P('|---|---|---|---|---|')
for k, a, b in sorted(movers, key=lambda m: norm(m[2])-m[1]):
    P(f'| `{k}` | {a:.1f} | {b:.1f} | {norm(b):.1f} | {norm(b)-a:+.1f} |')
P('')
peak = [k for k in nkeys if new_c[k].get('reduced') and float(new_c[k]['reduced']['peak_block_pct']) > 100]
P(f'- peak block over the 5.333 ms budget at eco: {len(peak)} effects '
  f'(was {len([k for k in okeys if old_c[k].get("reduced") and float(old_c[k]["reduced"]["peak_block_pct"]) > 100])})\n')
bad_status = [f'{k}/{q}: {r["status"]}' for k in nkeys for q, r in new_c[k].items() if r['status'] != 'ok']
P(f'- bench rows with status != ok: {", ".join(bad_status) if bad_status else "none"}\n')

# ---- broken / silent ----------------------------------------------------
def broken(d):
    why = []
    if d.get('nan'): why.append(f'NaN x{d["nan"]}')
    if d.get('silent'): why.append(f'silent ({d["rms_db"]:.1f} dB)')
    if d.get('peak', 0) > 1.0: why.append(f'peak {d["peak"]:.2f} > 0 dBFS')
    if d.get('clip_frac', 0) > 0: why.append(f'clip {d["clip_frac"]:.4f}')
    if abs(d.get('dc', 0)) > 0.01: why.append(f'DC {d["dc"]:+.3f}')
    return why
P('## Dead / silent / broken renders\n')
for label, o, n in (('DI renders', om, nm), ('probe renders', op, np_)):
    rows = []
    for name in sorted(n):
        if name == 'DI': continue
        w = broken(n[name])
        if not w: continue
        wo = broken(o[name]) if name in o else None
        tag = 'new effect' if name not in o else ('NEW' if not wo else 'pre-existing')
        rows.append(f'| `{name}` | {", ".join(w)} | {tag} |')
    P(f'### {label}: {len(rows)} flagged\n')
    if rows:
        P('| render | why | status |'); P('|---|---|---|'); out.extend(rows)
    P('')
fixed = [name for name in om if name != 'DI' and broken(om[name]) and name in nm and not broken(nm[name])]
fixed += [name for name in op if broken(op[name]) and name in np_ and not broken(np_[name])]
P(f'- previously flagged, now clean ({len(fixed)}): ' + (', '.join(f'`{x}`' for x in sorted(fixed)) or 'none') + '\n')

# recipe variants that render identical to default = inert gain control
P('### Inert recipe controls (hot/crunch render == default within 0.1 dB, all bands)\n')
inert = []
for name in sorted(nm):
    if '-' not in name or name.endswith('-cab'): continue
    b = base(name)
    if b not in nm: continue
    d0, d1 = nm[b], nm[name]
    if abs(d0['rms_db'] - d1['rms_db']) < 0.1 and max(abs(x - y) for x, y in zip(d0['band_delta_db'], d1['band_delta_db'])) < 0.1:
        inert.append(f'`{name}`')
P(', '.join(inert) if inert else 'none'); P('')

# ---- unity --------------------------------------------------------------
di = nm['DI']['rms_db']; di_o = om['DI']['rms_db']
P('## Unity (default render rms vs DI rms)\n')
uo = {k: om[k]['rms_db'] - di_o for k in om if k != 'DI' and '-' not in k}
un = {k: nm[k]['rms_db'] - di for k in nm if k != 'DI' and '-' not in k}
off_o = {k for k, v in uo.items() if abs(v) > UNITY_TOL}
off_n = {k for k, v in un.items() if abs(v) > UNITY_TOL}
P(f'- off unity by > {UNITY_TOL:.0f} dB: {len(off_o)}/{len(uo)} -> {len(off_n)}/{len(un)} '
  f'(existing effects: {len(off_n & set(uo))}/{len(uo)}; new effects: {len(off_n - set(uo))}/{len(set(un)-set(uo))})')
P('- note: delays/reverbs/modulation mix dry+wet and amps without cab are expected to read off-unity; '
  'the actionable rows are drives/boosters/comps/EQs (calibrated by PedalUnityLevel). '
  'The repo DI is quiet (rms ~-39 dBFS), so high-gain pedals read well above it at defaults; '
  'compare against the prev column rather than reading the absolute number as a defect.\n')
UNITY_CLASS = ('Overdrive', 'Distortion', 'Fuzz', 'Booster', 'Compressor', 'Equalizer', 'ParametricEQ', 'MtCmp_')
act = sorted((k for k in off_n if any(t in k for t in UNITY_CLASS)), key=lambda k: -abs(un[k]))
P(f'### Calibrated class (drive/boost/comp/EQ) off unity by > {UNITY_TOL:.0f} dB now: {len(act)}\n')
P('| effect | dB vs DI | prev dB |'); P('|---|---|---|')
for k in act:
    P(f'| `{k}` | {un[k]:+.1f} | {f"{uo[k]:+.1f}" if k in uo else "new"} |')
P('')
moved = [(k, uo[k], un[k]) for k in uo if k in un and abs(un[k] - uo[k]) >= UNITY_TOL]
P(f'### Unity shifts >= {UNITY_TOL:.0f} dB, existing effects ({len(moved)})\n')
P('| effect | prev dB | now dB | delta |'); P('|---|---|---|---|')
for k, a, b in sorted(moved, key=lambda m: m[2]-m[1]):
    P(f'| `{k}` | {a:+.1f} | {b:+.1f} | {b-a:+.1f} |')
P('')
P('### New effects, unity at defaults (worst 25 by |dB|)\n')
P('| effect | dB vs DI |'); P('|---|---|')
for k in sorted((k for k in un if k not in uo), key=lambda k: -abs(un[k]))[:25]:
    P(f'| `{k}` | {un[k]:+.1f} |')
P('')

# ---- THD / band ---------------------------------------------------------
P('## THD / even:odd shifts (sine220 probe, existing effects)\n')
P(f'Listed when THD moved by >= {THD_MOVE:.0f}% relative AND >= 0.5 pp absolute, or the even:odd dominance flipped.\n')
P('| probe | THD % prev | THD % now | even:odd prev | even:odd now |'); P('|---|---|---|---|---|')
for name in sorted(np_):
    if 'sine220' not in name or name not in op: continue
    a, b = op[name], np_[name]
    ta, tb = a.get('thd_pct', 0), b.get('thd_pct', 0)
    rel = abs(tb - ta) / max(ta, 1e-6) * 100
    ea, eb = a.get('even_odd', 0), b.get('even_odd', 0)
    flip = (ea > 1) != (eb > 1) and (ta > 0.5 or tb > 0.5)
    if (rel >= THD_MOVE and abs(tb - ta) >= 0.5) or flip:
        P(f'| `{name.split("__")[0]}` | {ta:.2f} | {tb:.2f} | {ea:.2f} | {eb:.2f}{" (flip)" if flip else ""} |')
P('')
P(f'## EQ signature shifts (DI band delta, any band >= {BAND_MOVE:.0f} dB, existing renders)\n')
P('Bands: 20-120 / 120-400 / 400-800 / 800-1.6k / 1.6-3.2k / 3.2-8k / 8-20k Hz.\n')
P('| render | worst band | prev dB | now dB | delta |'); P('|---|---|---|---|---|')
bn = ['20-120', '120-400', '400-800', '800-1.6k', '1.6-3.2k', '3.2-8k', '8-20k']
rows = []
for name in nm:
    if name == 'DI' or name not in om: continue
    d = [y - x for x, y in zip(om[name]['band_delta_db'], nm[name]['band_delta_db'])]
    i = max(range(len(d)), key=lambda j: abs(d[j]))
    if abs(d[i]) >= BAND_MOVE:
        rows.append((abs(d[i]), f'| `{name}` | {bn[i]} | {om[name]["band_delta_db"][i]:+.1f} | {nm[name]["band_delta_db"][i]:+.1f} | {d[i]:+.1f} |'))
for _, r in sorted(rows, reverse=True): P(r)
P('')

# ---- drive sweep --------------------------------------------------------
ds_o, ds_n = old_m.get('drive_sweep', {}), new_m.get('drive_sweep', {})
if ds_n:
    P('## Drive sweep\n')
    P('| family | max THD spread prev (pp) | now | max band spread prev (dB) | now |'); P('|---|---|---|---|---|')
    for fam, dv in ds_n.get('divergence', {}).items():
        po = ds_o.get('divergence', {}).get(fam, {})
        g = lambda d, k: d.get(k, '-')
        P(f'| {fam} | {g(po,"max_thd_spread_pct")} | {g(dv,"max_thd_spread_pct")} | {g(po,"max_band_spread_db")} | {g(dv,"max_band_spread_db")} |')
    P('')
    P('### Inert gain controls (THD and rms flat across the whole drive travel)\n')
    flat = []
    for key, r in ds_n.get('results', {}).items():
        s = r.get('settings', {})
        th = [v.get('thd_pct', 0) for v in s.values()]; rm = [v.get('rms_db', 0) for v in s.values()]
        if th and max(th) - min(th) < 0.5 and max(rm) - min(rm) < 0.5:
            flat.append(f'`{key}` (THD {min(th):.2f}-{max(th):.2f}%, rms spread {max(rm)-min(rm):.2f} dB)')
    P(', '.join(flat) if flat else 'none'); P('')
print('\n'.join(out))
