"""Measure speech preservation/noise attenuation in local RNNoise probe WAVs."""
import argparse
import json
from pathlib import Path
import math
import numpy as np
from scipy.io import wavfile
from scipy.signal import correlate, correlation_lags, resample_poly

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
args = parser.parse_args()

def read(path):
    rate, data = wavfile.read(path)
    if np.issubdtype(data.dtype, np.integer):
        data = data.astype(np.float64) / (2 ** (data.dtype.itemsize * 8 - 1))
    return rate, data.astype(np.float64)

def rms(data):
    return float(np.sqrt(np.mean(np.square(data))))

rows = json.loads((args.directory / "processing.json").read_text(encoding="utf-8-sig"))
for row in rows:
    rate = row["rate"]
    _, clean = read(args.directory / f"clean-{rate}.wav")
    _, noisy = read(args.directory / f"input-{rate}.wav")
    _, output = read(args.directory / f"output-{rate}.wav")
    analysis_rate = min(16000, rate)
    divisor = math.gcd(rate, analysis_rate)
    reference = resample_poly(clean, analysis_rate // divisor, rate // divisor)
    measured = resample_poly(output, analysis_rate // divisor, rate // divisor)
    correlation = correlate(measured, reference, method="fft")
    lags = correlation_lags(len(measured), len(reference))
    valid = (lags >= 0) & (lags <= analysis_rate // 5)
    lag = int(lags[valid][np.argmax(correlation[valid])])
    aligned = measured[lag:lag + len(reference)]
    reference = reference[:len(aligned)]
    speech = slice(int(1.1 * analysis_rate), len(reference) - analysis_rate)
    score = float(np.dot(reference[speech], aligned[speech]) /
                  np.sqrt(np.dot(reference[speech], reference[speech]) * np.dot(aligned[speech], aligned[speech])))
    shift = int(round(lag * rate / analysis_rate))
    in_noise = rms(noisy[int(0.5 * rate):int(0.85 * rate)])
    out_noise = rms(output[shift + int(0.5 * rate):shift + int(0.85 * rate)])
    suppression = min(100.0, 20 * math.log10(max(in_noise, 1e-12) / max(out_noise, 1e-12)))
    speech_rms = rms(aligned[speech])
    row.update(noise_attenuation_db=suppression, speech_correlation=score,
               speech_output_rms=speech_rms, measured_alignment_ms=lag * 1000 / analysis_rate,
               realtime_compute_ratio=row["processing_seconds"] / row["audio_seconds"])
    row["latency_error_ms"] = row["measured_alignment_ms"] - row["reported_latency_samples"] * 1000 / rate
    row["passed"] = (suppression >= 10 and score >= 0.5 and speech_rms >= 0.001
                     and abs(row["latency_error_ms"]) < 0.5)
    print(f'{rate:6d} Hz: noise {suppression:6.1f} dB, speech correlation {score:.4f}, '
          f'align {row["measured_alignment_ms"]:.3f} ms, CPU ratio {row["realtime_compute_ratio"]:.4f}, '
          f'{"PASS" if row["passed"] else "FAIL"}')
(args.directory / "analysis.json").write_text(json.dumps(rows, indent=2), encoding="utf-8")
if not all(row["passed"] for row in rows):
    raise SystemExit("Non-native processing evidence failed")
