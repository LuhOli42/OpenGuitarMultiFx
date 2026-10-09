#!/usr/bin/env python3
"""Generates the render job list for the FX audit and runs them in parallel."""
import subprocess, sys, os, re, csv, concurrent.futures

BIN = os.environ.get('RENDERFX_BIN', 'build/Tests/OpenGuitarMultiFx_RenderFx_artefacts/Release/OpenGuitarMultiFx_RenderFx')
DI = os.environ.get('DI_WAV', 'Tests/fixtures/di/guitar-di.wav')
NAM = os.environ.get('NAM_FIXTURE', 'Tests/fixtures/lstm.nam')
CABIR = os.environ.get('CAB_IR', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ir', 'cab-4x12.wav'))
OUT = '/home/ubuntu/fx-audit/out'
os.makedirs(OUT, exist_ok=True)

ALL_KEYS = """AC15StyleAmplifier AC30StyleAmplifier Ambient AnalogDelay BD2StyleOverdrive BassmanStyleAmplifier
BigMuffStyleFuzz BluesBreakerStyleOverdrive Cab CentaurStyleOverdrive Chorus Compressor
CrunchBoxStyleDistortion DOD250StyleOverdrive DS1StyleDistortion DS201StyleNoiseGate DT1StyleDistortion
Dbx160StyleCompressor DeluxeReverbStyleAmplifier DigitalDelay DistortionPlusStyleDistortion DualDelay
DualRectifierStyleAmplifier DynaCompStyleCompressor DynamicCab ENGLPowerballStyleAmplifier EPStyleBooster
EVH5150StyleAmplifier Flanger FuzzFaceStyleFuzz GE7StyleEqualizer GSeriesStyleBusCompressor GatedReverb
GuvnorStyleDistortion HM2StyleDistortion Hall Harmonizer Hold JC120StyleAmplifier JCM800StyleAmplifier
JTM45StyleAmplifier La2aStyleCompressor Looper MarkIICPlusStyleAmplifier MetalZoneStyleDistortion
MultiTapDelay NS2StyleNoiseSuppressor NeuralAmp NeuralAmpCab NeuralPedal NoiseGate OCDStyleOverdrive
OD1StyleOverdrive ODR1StyleOverdrive Octaver OverdriverStyleOverdrive ParametricEQ Phaser PingPong
PitchMod PitchShift Plate PositiveGroundBooster RAT2StyleDistortion RatStyleDistortion Reverb
ReverseDelay RingMod RockerverbStyleAmplifier Room RossStyleCompressor Rotary RussianBigMuffStyleFuzz
SLO100StyleAmplifier Shimmer SiliconFuzzFaceStyleFuzz SiliconToneBenderStyleFuzz SovtekBigMuffStyleFuzz
Spring SqueezerStyleCompressor SuperLeadStyleAmplifier TS10StyleOverdrive TS808StyleOverdrive
TS9StyleOverdrive TapeDelay ToneBenderStyleFuzz Tremolo TubeDriverStyleOverdrive TurboRatStyleDistortion
TwinReverbStyleAmplifier UniVibe Urei1176StyleCompressor Vibrato ZendriveStyleOverdrive""".split()

# main gain/distortion param pushed for the "hot" audition variant
HOT = {
 'TS808StyleOverdrive':'ts808_drive=0.85','TS9StyleOverdrive':'ts9_drive=0.85','TS10StyleOverdrive':'ts10_drive=0.85',
 'CentaurStyleOverdrive':'centaur_gain=0.85','BD2StyleOverdrive':'bd2_gain=0.85','OD1StyleOverdrive':'od1_drive=0.85',
 'ODR1StyleOverdrive':'odr1_drive=0.85','DS1StyleDistortion':'ds1_drive=0.85','DT1StyleDistortion':'dt1_distortion=0.85',
 'OCDStyleOverdrive':'ocd_drive=0.85','ZendriveStyleOverdrive':'zendrive_drive=0.85',
 'BluesBreakerStyleOverdrive':'bluesbreaker_drive=0.85','CrunchBoxStyleDistortion':'crunchbox_drive=0.85',
 'DOD250StyleOverdrive':'dod250_distortion=0.85','DistortionPlusStyleDistortion':'dp_distortion=0.85',
 'OverdriverStyleOverdrive':'overdriver_gain=0.9','TubeDriverStyleOverdrive':'tubedriver_drive=0.85',
 'GuvnorStyleDistortion':'guvnor_gain=0.85','MetalZoneStyleDistortion':'mt2_dist=0.85','HM2StyleDistortion':'hm2_dist=0.85',
 'RatStyleDistortion':'rat_distortion=0.85','RAT2StyleDistortion':'rat_distortion=0.85','TurboRatStyleDistortion':'rat_distortion=0.85',
 'BigMuffStyleFuzz':'bmp_sustain=0.85','SovtekBigMuffStyleFuzz':'bmpsv_sustain=0.85','RussianBigMuffStyleFuzz':'bmpru_sustain=0.85',
 'FuzzFaceStyleFuzz':'ff_fuzz=1.0','SiliconFuzzFaceStyleFuzz':'ffsi_fuzz=1.0',
 'ToneBenderStyleFuzz':'tb_attack=1.0','SiliconToneBenderStyleFuzz':'tbsi_attack=1.0',
 'PositiveGroundBooster':'posboost_boost=0.9','EPStyleBooster':'epbooster_level=0.9',
}

AMPS = {
 'AC15StyleAmplifier':'a15_volume=0.85','AC30StyleAmplifier':'a30_volume=0.85',
 'BassmanStyleAmplifier':'bm_vol_normal=0.9 bm_vol_bright=0.2',
 'TwinReverbStyleAmplifier':'tr_volume=0.9 tr_n_volume=0.2',
 'DeluxeReverbStyleAmplifier':'dr_volume=0.9 dr_n_volume=0.2',
 'JC120StyleAmplifier':'jc_volume=0.8 jc_distortion=0.6',
 'JTM45StyleAmplifier':'jm_vol_normal=0.9 jm_vol_bright=0.3',
 'JCM800StyleAmplifier':'j8_gain=0.85','SuperLeadStyleAmplifier':'sl_loud1=0.9 sl_loud2=0.3',
 'SLO100StyleAmplifier':'slo_channel=1 slo_gain=0.85','MarkIICPlusStyleAmplifier':'mk2c_channel=1 mk2c_gain=0.85',
 'DualRectifierStyleAmplifier':'drec_channel=2 drec_gain=0.85','EVH5150StyleAmplifier':'evh_channel=1 evh_gain=0.85',
 'ENGLPowerballStyleAmplifier':'engl_pb_channel=3 engl_pb_gain=0.85',
 'RockerverbStyleAmplifier':'rv_channel=1 rv_gain=0.85',
}

def model_args(key):
    if key.startswith('Neural'): return ['--model=' + NAM]
    if key in ('Cab', 'DynamicCab'): return ['--model=' + CABIR]
    return []

jobs = []  # (outname, infile, keystring, extra_args)
for k in ALL_KEYS:
    jobs.append((k, DI, k, model_args(k) + ['--repeat=3']))
for k, p in HOT.items():
    jobs.append((k + '-hot', DI, k, [p] + model_args(k)))
for k, p in AMPS.items():
    jobs.append((k + '-crunch', DI, k, p.split()))
    jobs.append((k + '-cab', DI, k + '+Cab', p.split() + ['--model=' + CABIR]))

def run(job):
    outname, inf, key, extra = job
    out = f'{OUT}/{outname}.wav'
    cmd = [BIN, inf, out, key, '--quality=high'] + extra
    try:
        r = subprocess.run(cmd, capture_output=True, text=True, timeout=1200)
        return outname, key, r.stdout.strip(), r.stderr.strip()[-400:], r.returncode
    except subprocess.TimeoutExpired:
        return outname, key, '', 'TIMEOUT', -1

results = []
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as ex:
    for res in ex.map(run, jobs):
        results.append(res)
        print(res[0], '|', res[2][:160], ('STDERR ' + res[3][:200]) if res[3] else '', flush=True)

with open('/home/ubuntu/fx-audit/renders.csv', 'w', newline='') as f:
    w = csv.writer(f)
    w.writerow(['variant', 'key', 'stdout', 'stderr', 'rc'])
    w.writerows(results)
print('DONE', len(results))
