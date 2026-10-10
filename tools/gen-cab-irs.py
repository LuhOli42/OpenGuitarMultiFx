#!/usr/bin/env python3
"""Regenerate the bundled cabinet impulse responses in Assets/IRs/.

These are SYNTHETIC "house curve" IRs: each is the impulse response of a
small cascade of analog-style filters (high-pass for the cabinet's low-end
rolloff, a peaking bump for the box resonance, a presence bump, and a
4th-order low-pass for the cone/mic HF rolloff), plus a faint early-echo
comb for the edge-diffraction texture a close mic picks up off a real cone.
They are not measurements of any real cabinet and contain no third-party
IR material (licensing-clean). See docs/BundledCabIRs.md.

Pure stdlib -- no numpy/scipy needed:

    python3 tools/gen-cab-irs.py
"""
import math
import os
import struct
import wave

FS = 48000
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "Assets", "IRs")


def biquad_peaking(f0, gain_db, q):
    a = 10.0 ** (gain_db / 40.0)
    w0 = 2.0 * math.pi * f0 / FS
    alpha = math.sin(w0) / (2.0 * q)
    cosw = math.cos(w0)
    b0 = 1.0 + alpha * a
    b1 = -2.0 * cosw
    b2 = 1.0 - alpha * a
    a0 = 1.0 + alpha / a
    a1 = -2.0 * cosw
    a2 = 1.0 - alpha / a
    return (b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0)


def biquad_lowpass(fc, q):
    w0 = 2.0 * math.pi * fc / FS
    alpha = math.sin(w0) / (2.0 * q)
    cosw = math.cos(w0)
    b0 = (1.0 - cosw) / 2.0
    b1 = 1.0 - cosw
    b2 = b0
    a0 = 1.0 + alpha
    a1 = -2.0 * cosw
    a2 = 1.0 - alpha
    return (b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0)


def biquad_highpass(fc, q):
    w0 = 2.0 * math.pi * fc / FS
    alpha = math.sin(w0) / (2.0 * q)
    cosw = math.cos(w0)
    b0 = (1.0 + cosw) / 2.0
    b1 = -(1.0 + cosw)
    b2 = b0
    a0 = 1.0 + alpha
    a1 = -2.0 * cosw
    a2 = 1.0 - alpha
    return (b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0)


def lp4(fc):
    # 4th-order Butterworth low-pass as two biquads.
    return [biquad_lowpass(fc, 0.5412), biquad_lowpass(fc, 1.3065)]


def apply_biquad(x, coef):
    b0, b1, b2, a1, a2 = coef
    y = [0.0] * len(x)
    x1 = x2 = y1 = y2 = 0.0
    for i, xn in enumerate(x):
        yn = b0 * xn + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2
        y[i] = yn
        x2, x1 = x1, xn
        y2, y1 = y1, yn
    return y


def make_ir(cascade, n_samples, comb=(0.22, 16, 0.11, 37), fade_frac=0.3):
    """Impulse response of the biquad cascade, with a faint early-echo comb
    (direct cone + edge diffraction at the mic), tail faded to zero."""
    pad = max(comb[1], comb[3]) + 1
    x = [0.0] * (n_samples + pad)
    x[0] = 1.0
    h = x
    for coef in cascade:
        h = apply_biquad(h, coef)
    g1, d1, g2, d2 = comb
    for i in range(d1, len(h)):
        h[i] += g1 * h[i - d1]
    for i in range(d2, len(h)):
        h[i] += g2 * h[i - d2]
    h = h[:n_samples]
    fade_start = int(n_samples * (1.0 - fade_frac))
    for i in range(fade_start, n_samples):
        t = (i - fade_start) / (n_samples - fade_start)
        h[i] *= 0.5 * (1.0 + math.cos(math.pi * t))
    peak = max(abs(v) for v in h) or 1.0
    return [v * 0.5 / peak for v in h]


def write_wav(path, samples):
    frames = bytearray()
    for v in samples:
        s = int(max(-1.0, min(1.0, v)) * 8388607.0)
        frames += struct.pack("<i", s)[:3]  # 24-bit little-endian
    for channel in (0, 1):  # duplicate below instead
        pass
    stereo = bytearray()
    for v in samples:
        s = int(max(-1.0, min(1.0, v)) * 8388607.0)
        packed = struct.pack("<i", s)[:3]
        stereo += packed + packed  # same capture on L and R, like one close mic
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(3)
        w.setframerate(FS)
        w.writeframes(bytes(stereo))


CABS = {
    # filename: (high-pass Hz, box bump (Hz,dB,Q), presence (Hz,dB,Q),
    #            extra notch (Hz,dB,Q) or None, low-pass Hz, length ms)
    "cab-4x12-closed.wav": (
        75.0, (100.0, 4.0, 1.1), (2700.0, 2.0, 1.4), (4300.0, -2.5, 1.8), 5200.0, 45),
    "cab-2x12-open.wav": (
        95.0, (125.0, 2.5, 1.3), (2500.0, 1.5, 1.3), None, 6200.0, 35),
    "cab-1x12-open.wav": (
        110.0, (150.0, 2.0, 1.4), (3000.0, 2.0, 1.5), None, 6800.0, 30),
}


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    for name, (fc_hp, bump, pres, notch, fc_lp, length_ms) in CABS.items():
        cascade = [biquad_highpass(fc_hp, 0.7071)]
        cascade.append(biquad_peaking(*bump))
        cascade.append(biquad_peaking(*pres))
        if notch is not None:
            cascade.append(biquad_peaking(*notch))
        cascade += lp4(fc_lp)
        n = int(FS * length_ms / 1000.0)
        ir = make_ir(cascade, n)
        path = os.path.join(OUT_DIR, name)
        write_wav(path, ir)
        print(f"{name}: {n} samples ({length_ms} ms), HP {fc_hp} Hz, "
              f"bump +{bump[1]} dB @ {bump[0]} Hz, LP {fc_lp} Hz")


if __name__ == "__main__":
    main()
