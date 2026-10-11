#!/usr/bin/env python3
"""CPU benchmark driver for the A7S port (Phase 6): runs `RenderFx --bench`
per registered effect and collects one machine-readable CSV plus a markdown
table. Same style as run_renders.py -- but each job is a benchmark, so the
worker count is deliberately low to keep cores uncontended (every job is
single-threaded; on the A7S pin the whole run to the big cores with
BENCH_CORESET, e.g. `BENCH_CORESET=0,1`).

    python3 tools/fx-audit/run_cpu_bench.py

Environment overrides:
    RENDERFX_BIN   bench binary      (default: Release artefact path)
    DI_WAV         input signal      (default: Tests/fixtures/di/guitar-di.wav)
    NAM_FIXTURE    .nam for Neural*  (default: Tests/fixtures/lstm.nam)
    CAB_IR         IR for Cab        (default: tools/fx-audit/ir/cab-4x12.wav)
    BENCH_REPS     measured reps     (default: 3, best-of like run_renders)
    BENCH_WORKERS  parallel jobs     (default: 4, or the core count when
                   BENCH_CORESET is set -- benchmarking is timing-sensitive)
    BENCH_CORESET  Linux CPU list    (e.g. "0,1" -> os.sched_setaffinity)
    BENCH_CSV      csv output path   (default: tools/fx-audit/cpu-bench.csv)
    BENCH_MD       md report path    (default: <BENCH_CSV minus .csv>.md)
"""
import subprocess, sys, os, re, csv, concurrent.futures, platform, datetime

BIN = os.environ.get('RENDERFX_BIN', 'build/Tests/OpenGuitarMultiFx_RenderFx_artefacts/Release/OpenGuitarMultiFx_RenderFx')
DI = os.environ.get('DI_WAV', 'Tests/fixtures/di/guitar-di.wav')
NAM = os.environ.get('NAM_FIXTURE', 'Tests/fixtures/lstm.nam')
CABIR = os.environ.get('CAB_IR', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ir', 'cab-4x12.wav'))
REPS = os.environ.get('BENCH_REPS', '3')
CORESET = os.environ.get('BENCH_CORESET', '')
# Pinned runs must not over-subscribe the allowed cores -- two benchmarks
# racing for one core inflate each other's CPU% and peak-block times.
_default_workers = str(len(CORESET.split(','))) if CORESET else '4'
WORKERS = int(os.environ.get('BENCH_WORKERS', _default_workers))
CSV_OUT = os.environ.get('BENCH_CSV', 'tools/fx-audit/cpu-bench.csv')
MD_OUT = os.environ.get('BENCH_MD', re.sub(r'\.csv$', '', CSV_OUT) + '.md')
FLAG_CPU_PCT = float(os.environ.get('BENCH_FLAG_CPU_PCT', '10.0'))  # A7S reduced-quality budget

if CORESET and hasattr(os, 'sched_setaffinity'):
    os.sched_setaffinity(0, {int(c) for c in CORESET.split(',')})
    print(f'pinned bench to cores: {CORESET}', file=sys.stderr)

def list_keys():
    r = subprocess.run([BIN, '--list'], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f'{BIN} --list failed:\n{r.stderr}')
    return [line.split()[1] for line in r.stdout.splitlines() if line.startswith('FX ')]

def model_args(key):
    if key.startswith('Neural'): return ['--model=' + NAM]
    if key in ('Cab', 'DynamicCab', 'Reverb'): return ['--model=' + CABIR]
    return []

def run(key):
    cmd = [BIN, '--bench', DI, '-', key, '--repeat=' + REPS] + model_args(key)
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=3600)
        rows = [l for l in r.stdout.splitlines() if l and not l.startswith('effect,')]
        return key, rows, r.stderr.strip()[-400:], r.returncode
    except subprocess.TimeoutExpired:
        return key, [], 'TIMEOUT', -1

keys = list_keys()
for f, why in [(BIN, 'binary'), (DI, 'DI wav')]:
    if not os.path.exists(f): sys.exit(f'missing {why}: {f}')
for f in (NAM, CABIR):
    if not os.path.exists(f): print(f'warning: missing model fixture {f} (Neural*/Cab rows will measure ~0 CPU)', file=sys.stderr)

rows, failures = [], []
with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as ex:
    for key, krows, err, rc in ex.map(run, keys):
        rows.extend(krows)
        for r in krows:
            if r.split(',')[-1] != 'ok':
                failures.append(r)
        if rc != 0 or len(krows) != 2:
            failures.append(f'{key}: rc={rc} rows={len(krows)}')
        print(key, '|', (err.splitlines()[-1][:160] if err else ''), ('rc=' + str(rc)) if rc else '', flush=True)

with open(CSV_OUT, 'w', newline='') as f:
    f.write('effect,quality,sr,block,channels,reps,audio_ms,proc_ms,cpu_pct,rtf,peak_block_ms,budget_ms,peak_block_pct,blocks_over_budget,status\n')
    f.write('\n'.join(rows) + '\n')
print('wrote', CSV_OUT, len(rows), 'rows')

# ---- markdown report ---------------------------------------------------------
data = {}
for l in rows:
    cols = l.split(',')
    data.setdefault(cols[0], {})[cols[1]] = cols

def cpu(entry): return float(entry[8]) if entry and entry[8] else 0.0
def cell(entry, idx): return entry[idx] if entry else '-'

# Sort by reduced-quality CPU descending -- that is the A7S column.
order = sorted(data.keys(), key=lambda k: -cpu(data[k].get('reduced')))
flagged = [k for k in order if cpu(data[k].get('reduced')) > FLAG_CPU_PCT]
peakflag = [k for k in order if data[k].get('reduced') and float(cell(data[k]['reduced'], 12)) > 100.0]

lines = []
lines.append(f'# CPU benchmark — {datetime.date.today().isoformat()}')
lines.append('')
lines.append('Host: `' + platform.machine() + ' ' + platform.processor() + '` — `RenderFx --bench`, '
             f'{REPS} reps (best-of), DI `{DI}`.')
lines.append('')
lines.append('Settings: 48 kHz, mono, 256-sample blocks (5.333 ms/block budget). '
             'CPU% = processing_time / audio_time; `peak` is the slowest single block in the fastest rep '
             '(a >100% peak means at least one realtime callback would have been late). '
             '`high` = OversamplingQuality::high (desktop reference); `reduced` = the tier the app ships '
             'on the A7S (OversamplingQuality::eco, minimal oversampling — amps already run their '
             'reduced-order power stage in every tier).')
lines.append('')
lines.append('| effect | cpu% high | cpu% reduced | peak block high (ms) | peak block reduced (ms) |')
lines.append('|---|---|---|---|---|')
for k in order:
    h, r = data[k].get('high'), data[k].get('reduced')
    mark = ' **(>{}% flag)**'.format(FLAG_CPU_PCT) if k in flagged else ''
    lines.append(f'| {k}{mark} | {cell(h,8)} | {cell(r,8)} | {cell(h,10)} | {cell(r,10)} |')
lines.append('')
lines.append(f'## >{FLAG_CPU_PCT}% reduced-quality flags ({len(flagged)})')
lines.append('')
for k in flagged:
    lines.append(f'- **{k}** — {cell(data[k]["reduced"],8)}% cpu, {cell(data[k]["reduced"],10)} ms peak block')
lines.append('')
lines.append(f'## peak-block-over-budget at reduced ({len(peakflag)})')
lines.append('')
for k in peakflag:
    lines.append(f'- **{k}** — peak {cell(data[k]["reduced"],10)} ms vs {cell(data[k]["reduced"],11)} ms budget '
                 f'({cell(data[k]["reduced"],13)} late blocks in best rep)')
lines.append('')

with open(MD_OUT, 'w') as f:
    f.write('\n'.join(lines))
print('wrote', MD_OUT)
print('flagged:', ', '.join(flagged) if flagged else 'none')
if failures:
    print('FAILED ROWS (outputs written but table is incomplete):', file=sys.stderr)
    for f_ in failures:
        print(' ', f_, file=sys.stderr)
    sys.exit(1)
print('DONE')
