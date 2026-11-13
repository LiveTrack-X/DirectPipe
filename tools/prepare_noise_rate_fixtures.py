"""Prepare deterministic, local speech/noise fixtures from an existing WAV.

No recording or networking. Used by the opt-in RNNoise evidence test.
"""
from pathlib import Path
import argparse
import math
import numpy as np
from scipy.io import wavfile
from scipy.signal import resample_poly

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
args = parser.parse_args()
rate, speech = wavfile.read(args.directory / "speech-original.wav")
if speech.ndim > 1:
    speech = speech.mean(axis=1)
speech = speech.astype(np.float64) / max(1.0, np.max(np.abs(speech.astype(np.float64))))
speech = resample_poly(speech, 48000 // math.gcd(rate, 48000), rate // math.gcd(rate, 48000)) * 0.35
clean = np.concatenate([np.zeros(48000), speech, np.zeros(48000)])
noise = np.random.default_rng(451).normal(0, 0.018, len(clean))
noisy = clean + noise
for target in (8000, 16000, 32000, 44100, 48000, 88200, 96000, 192000):
    divisor = math.gcd(target, 48000)
    for name, data in (("clean", clean), ("input", noisy)):
        converted = resample_poly(data, target // divisor, 48000 // divisor)
        wavfile.write(args.directory / f"{name}-{target}.wav", target, converted.astype(np.float32))
print(f"Prepared {len(clean) / 48000:.3f}s speech/noise at 8 sample rates")
