#!/usr/bin/env python3
"""Probe renders: sine THD, swept-sine frequency response, impulse tails."""
import subprocess, os, sys, csv, concurrent.futures

BIN = os.environ.get('RENDERFX_BIN', 'build/Tests/OpenGuitarMultiFx_RenderFx_artefacts/Release/OpenGuitarMultiFx_RenderFx')
A = os.environ.get('FX_AUDIT_DIR', '/tmp/fx-audit')
NAM = os.environ.get('NAM_FIXTURE', 'Tests/fixtures/lstm.nam')
CABIR = os.environ.get('CAB_IR', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'ir', 'cab-4x12.wav'))
PROBES_DIR = os.environ.get('PROBE_WAV_DIR', os.path.join(os.path.dirname(os.path.abspath(__file__)), 'probes'))
OUT = A + '/probes'
os.makedirs(OUT, exist_ok=True)

LINEAR = """AC15StyleAmplifier AC30StyleAmplifier Acoustic360StyleAmplifier
AmpegB15StyleAmplifier AmpegV4BStyleAmplifier Astroverb16StyleAmplifier BD2StyleOverdrive
BassBigMuffStyleFuzz BassmanStyleAmplifier BigMuffStyleFuzz BluesBreakerStyleOverdrive
BognerUberschallStyleAmplifier CarmenGhiaStyleAmplifier CarrRamblerStyleAmplifier CentaurStyleOverdrive
CrunchBoxStyleDistortion DOD250StyleOverdrive DS1StyleDistortion DT1StyleDistortion
DarkglassB7KStyleOverdrive DeluxeReverbStyleAmplifier DistortionPlusStyleDistortion
DividedBy13FTR37StyleAmplifier DualRectifierStyleAmplifier DumbleSteelStringStyleAmplifier
ENGLPowerballStyleAmplifier EPStyleBooster EVH5150StyleAmplifier FryetteDeliveranceD120StyleAmplifier
FuzzFaceStyleFuzz GE7StyleEqualizer GEB7StyleEqualizer GarnetHerzogStyleAmplifier
GibsonEH150StyleAmplifier GuvnorStyleDistortion HM2StyleDistortion HotCat30StyleAmplifier
BossOC2StyleOctaver
JC120StyleAmplifier JCM800StyleAmplifier JTM45StyleAmplifier KometConcordeStyleAmplifier
MarkIICPlusStyleAmplifier MatchlessHC30StyleAmplifier MesaBass400PlusStyleAmplifier
MetalZoneStyleDistortion MuTronIIIStyleFilter NeuralAmp NeuralAmpCab NeuralPedal
OCDStyleOverdrive OD1StyleOverdrive ODB3StyleOverdrive ODR1StyleOverdrive
OverdriverStyleOverdrive ParametricEQ PositiveGroundBooster RAT2StyleDistortion
RatStyleDistortion RiveraKnuckleheadStyleAmplifier RockerverbStyleAmplifier RussianBigMuffStyleFuzz
SLO100StyleAmplifier SVTStyleAmplifier SansAmpBDDIStyleOverdrive SiliconFuzzFaceStyleFuzz
SiliconToneBenderStyleFuzz SovtekBigMuffStyleFuzz SunnModelTStyleAmplifier SuperLeadStyleAmplifier
TS10StyleOverdrive TS808StyleOverdrive TS9BStyleOverdrive TS9StyleOverdrive
ToneBenderStyleFuzz TraceElliotGP12StyleAmplifier TrainwreckExpressStyleAmplifier
TubeDriverStyleOverdrive TurboRatStyleDistortion TwinReverbStyleAmplifier ZendriveStyleOverdrive""".split()

DYN = """Compressor DynaCompStyleCompressor Dbx160StyleCompressor GSeriesStyleBusCompressor
La2aStyleCompressor RossStyleCompressor SqueezerStyleCompressor Urei1176StyleCompressor
DS201StyleNoiseGate NS2StyleNoiseSuppressor NoiseGate GateProcessor""".split()
DYN = [d for d in DYN if d != 'GateProcessor']

TIME = """DigitalDelay TapeDelay AnalogDelay DualDelay MultiTapDelay PingPong ReverseDelay
Ambient Spring Hall Plate Room Shimmer GatedReverb Reverb Tremolo Chorus Vibrato
Flanger Phaser Rotary UniVibe PitchMod PitchShift Octaver Harmonizer
RingMod Hold""".split()

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
 'BassBigMuffStyleFuzz':'bbmp_sustain=0.85','DarkglassB7KStyleOverdrive':'b7k_drive=0.85',
 'ODB3StyleOverdrive':'odb3_gain=0.85','SansAmpBDDIStyleOverdrive':'bddi_drive=0.85',
 'TS9BStyleOverdrive':'ts9b_drive=0.85',
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
 'Acoustic360StyleAmplifier':'acoustic360_volume=0.85',
 'AmpegB15StyleAmplifier':'ampegb15_volume=0.85','AmpegV4BStyleAmplifier':'ampegv4b_volume=0.85',
 'Astroverb16StyleAmplifier':'astro16_preamp=0.85','BognerUberschallStyleAmplifier':'uber_gain=0.85',
 'CarmenGhiaStyleAmplifier':'cghia_volume=0.85','CarrRamblerStyleAmplifier':'cr_volume=0.85',
 'DividedBy13FTR37StyleAmplifier':'f37_ch1_volume=0.85','DumbleSteelStringStyleAmplifier':'sss_volume=0.85',
 'FryetteDeliveranceD120StyleAmplifier':'del_gain1=0.85','GarnetHerzogStyleAmplifier':'hzg_volume=0.85',
 'GibsonEH150StyleAmplifier':'eh150_volume=0.85','HotCat30StyleAmplifier':'hotcat30_gain=0.85',
 'KometConcordeStyleAmplifier':'kcd_volume=0.85','MatchlessHC30StyleAmplifier':'hc30_volume1=0.85',
 'MesaBass400PlusStyleAmplifier':'mesa400p_volume=0.85','RiveraKnuckleheadStyleAmplifier':'knk_gain=0.85',
 'SVTStyleAmplifier':'svt_gain=0.85','SunnModelTStyleAmplifier':'smt_vol_normal=0.9 smt_vol_brite=0.2',
 'TraceElliotGP12StyleAmplifier':'tegp12_input_gain=0.85','TrainwreckExpressStyleAmplifier':'twx_volume=0.85',
}

def model_args(key):
    if key.startswith('Neural'): return ['--model=' + NAM]
    return []

jobs = []
for k in LINEAR + DYN:
    for probe in ('probe-sine220.wav', 'probe-sine110.wav', 'probe-sweep.wav'):
        jobs.append((f'{k}__{probe[:-4]}', PROBES_DIR + '/' + probe, k, model_args(k)))
    if k in HOT:
        jobs.append((f'{k}-hot__probe-sine220', PROBES_DIR + '/probe-sine220.wav', k, [HOT[k]] + model_args(k)))
        jobs.append((f'{k}-hot__probe-sweep', PROBES_DIR + '/probe-sweep.wav', k, [HOT[k]] + model_args(k)))
    if k in AMPS:
        jobs.append((f'{k}-crunch__probe-sine220', PROBES_DIR + '/probe-sine220.wav', k, AMPS[k].split()))
        jobs.append((f'{k}-crunch__probe-sweep', PROBES_DIR + '/probe-sweep.wav', k, AMPS[k].split()))
for k in TIME:
    for probe in ('probe-clicks.wav', 'probe-sweep.wav'):
        jobs.append((f'{k}__{probe[:-4]}', PROBES_DIR + '/' + probe, k, ['--model=' + CABIR] if k == 'Reverb' else []))

# OC-2 is nonlinear so it lives in LINEAR for the sine probes, but it still gets
# the click transient probe like the other pitch/modulation effects.
jobs.append(('BossOC2StyleOctaver__probe-clicks', PROBES_DIR + '/probe-clicks.wav', 'BossOC2StyleOctaver', []))

def run(job):
    outname, inf, key, extra = job
    out = f'{OUT}/{outname}.wav'
    r = subprocess.run([BIN, inf, out, key] + extra, capture_output=True, text=True, timeout=1200)
    return outname, r.stdout.strip(), r.stderr.strip()[-200:], r.returncode

results = []
fails = []
with concurrent.futures.ThreadPoolExecutor(max_workers=6) as ex:
    for i, res in enumerate(ex.map(run, jobs)):
        results.append(res)
        if res[3] != 0 or res[2]:
            fails.append(res)
            print(res[0], '| rc', res[3], '|', res[2][:150], flush=True)

with open(A + '/probes.csv', 'w', newline='') as f:
    w = csv.writer(f); w.writerow(['variant', 'stdout', 'stderr', 'rc']); w.writerows(results)
print('DONE', len(results))

if fails:
    sys.exit(f'{len(fails)} probe renders failed')
