"""Offline comparison of actual C++ STFT prototypes and current host RNNoise.

No devices, networking, live application, or user recordings are accessed.
All metrics use the same clean-derived mask, declared delay, and original gain.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import subprocess
import sys

import numpy as np
from scipy.io import wavfile
from scipy.signal import butter, correlate, correlation_lags, resample_poly, sosfilt, sosfiltfilt

ROOT = Path(__file__).resolve().parents[2]
RATES = (44100, 48000)
CASES = ("hiss", "fan", "hum", "clicks", "clean")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def read_wav(path: Path) -> tuple[int, np.ndarray]:
    rate, data = wavfile.read(path)
    if np.issubdtype(data.dtype, np.integer):
        data = data.astype(np.float64) / 2 ** (8 * data.dtype.itemsize - 1)
    else:
        data = data.astype(np.float64)
    if data.ndim != 1:
        raise ValueError(f"Expected mono input: {path}")
    return rate, data


def noise_signal(kind: str, count: int, rate: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    white = rng.normal(size=count)
    t = np.arange(count) / rate
    if kind == "hiss":
        noise = white
    elif kind in ("fan", "clean"):
        colored = sosfilt(butter(2, 1100, fs=rate, output="sos"), white)
        colored /= max(float(np.std(colored)), 1e-12)
        noise = 0.8 * colored + 0.2 * white + 0.3 * np.sin(2 * np.pi * 120 * t)
    elif kind == "hum":
        noise = (np.sin(2 * np.pi * 60 * t) + 0.5 * np.sin(2 * np.pi * 120 * t)
                 + 0.25 * np.sin(2 * np.pi * 180 * t) + 0.12 * white)
    elif kind == "clicks":
        noise = 0.12 * white
        for onset in rng.integers(0, max(1, count - int(0.02 * rate)), size=max(1, int(count / rate * 4))):
            length = min(count - onset, int(0.015 * rate))
            noise[onset:onset + length] += rng.normal(size=length) * np.exp(-np.arange(length) / (rate * 0.002)) * 7
    else:
        raise ValueError(kind)
    return noise * (0.018 / max(float(np.sqrt(np.mean(noise**2))), 1e-12))


def fixture(speech: np.ndarray, rate: int, kind: str, index: int) -> tuple[np.ndarray, np.ndarray, np.ndarray, np.ndarray]:
    # One second explicit learning, half-second gain settling, voice, one second tail.
    prefix, tail = int(1.5 * rate), rate
    clean = np.zeros(prefix + len(speech) + tail)
    clean[prefix:prefix + len(speech)] = speech
    training = noise_signal(kind, rate, rate, 1600 + index)
    held_out = noise_signal(kind, len(clean) - rate, rate, 2600 + index)
    noise = np.concatenate((training, held_out))
    if kind == "clean":
        noise[prefix:] = 0
    noisy = clean + noise
    # Fixed clean-derived activity mask for all candidates. Relative -35 dB RMS,
    # computed over 10 ms, restricted to the original speech extent.
    window = max(1, int(rate * 0.01))
    energy = np.convolve(clean**2, np.ones(window) / window, mode="same")
    active = energy > max(float(np.max(energy)) * 10**(-35 / 10), 1e-10)
    active[:prefix] = False
    active[prefix + len(speech):] = False
    quiet = np.zeros(len(clean), dtype=bool)
    quiet[prefix + len(speech) + int(0.2 * rate):-int(0.1 * rate)] = True
    return clean, noisy, active, quiet


def db_ratio(top: float, bottom: float) -> float:
    return float(10 * np.log10(max(top, 1e-20) / max(bottom, 1e-20)))


def metrics(clean: np.ndarray, noisy: np.ndarray, output: np.ndarray,
            active: np.ndarray, quiet: np.ndarray, rate: int) -> dict:
    if len(clean) != len(output) or not np.all(np.isfinite(output)):
        raise ValueError("Nonfinite or truncated aligned output")
    reference, before, after = clean[active], noisy[active], output[active]
    signal = float(np.mean(reference**2))
    before_error = float(np.mean((before - reference)**2))
    after_error = float(np.mean((after - reference)**2))
    input_snr = db_ratio(signal, before_error) if before_error > 1e-18 else None
    result = {
        "active_input_snr_db": input_snr,
        "active_output_snr_db": db_ratio(signal, after_error),
        "active_snr_improvement_db": db_ratio(signal, after_error) - input_snr if input_snr is not None else None,
        "active_error_rms": float(np.sqrt(after_error)),
        "active_cosine_similarity": float(np.dot(reference, after) / max(1e-20, np.linalg.norm(reference) * np.linalg.norm(after))),
        "active_level_vs_clean_db": db_ratio(float(np.mean(after**2)), signal),
        "output_peak": float(np.max(np.abs(output))),
        "noise_only_attenuation_db": None,
        "noise_only_attenuation_lower_bound_db": None,
        "noise_only_output_below_measurement_floor": False,
    }
    quiet_input = float(np.mean(noisy[quiet]**2))
    if quiet_input > 1e-15:
        quiet_output = float(np.mean(output[quiet]**2))
        # RNNoise exports a 24-bit WAV. Use a common conservative one-LSB RMS
        # floor, including for float output; do not invent 165 dB measurements.
        floor_power = (2**-23)**2
        if quiet_output < floor_power:
            result["noise_only_output_below_measurement_floor"] = True
            result["noise_only_attenuation_lower_bound_db"] = db_ratio(quiet_input, floor_power)
        else:
            result["noise_only_attenuation_db"] = db_ratio(quiet_input, quiet_output)
    for label, low, high in (("low_voice", 80, 300), ("sibilance", 4000, 8000)):
        sos = butter(4, (low, high), fs=rate, btype="bandpass", output="sos")
        band_reference = sosfiltfilt(sos, clean)[active]
        band_output = sosfiltfilt(sos, output)[active]
        result[label + "_level_vs_clean_db"] = db_ratio(float(np.mean(band_output**2)), float(np.mean(band_reference**2)))
    return result


def run(command: list[str], log: Path | None = None, env: dict | None = None) -> None:
    result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
    if log:
        log.write_text(result.stdout + result.stderr, encoding="utf-8")
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}): {command}\n{result.stdout[-4000:]}\n{result.stderr[-2000:]}")


def measured_unity_delay(probe: Path, directory: Path, rate: int, size: int) -> tuple[int, float]:
    signal = np.zeros(4097, dtype=np.float32)
    signal[0] = 0.5; signal[-1] = -0.25
    source = directory / f"impulse-{rate}-{size}.f32"
    target = directory / f"impulse-{rate}-{size}-out.f32"
    source.write_bytes(signal.tobytes())
    run([str(probe), str(source), str(target), str(target.with_suffix(".json")), str(rate), str(size), "0", "learn", "0", "17"])
    output = np.fromfile(target, dtype=np.float32)
    measured = int(np.argmax(np.abs(output)))
    error = float(np.max(np.abs(output[size:size + len(signal)] - signal)))
    if measured != size or error > 2e-5:
        raise AssertionError("Measured delay/reconstruction disagrees with declared delay")
    return measured, error


def write_report(destination: Path, report: dict) -> None:
    """Refresh presentation/derived budget flags without rerunning DSP timings."""
    report["analysis_source_sha256"] = sha256(Path(__file__))
    report["timing_note"] = "Steady-clock elapsed wall time, including any preemption. Not thread CPU time. Overrun flags compare observed max with nominal 128-sample callback period, not actual dropout evidence."
    for row in report["rows"]:
        row["nominal_block_budget_ms"] = report["block_samples"] * 1000 / row["rate"]
        row["observed_max_exceeds_nominal_budget"] = row["max_block_ms"] > row["nominal_block_budget_ms"]
    (destination / "report.json").write_text(json.dumps(report, indent=2, allow_nan=False), encoding="utf-8")
    lines = ["# STFT voice-monitoring prototype evidence", "", report["protocol"], "",
             "All SNR metrics preserve original gain and use declared-delay alignment.", "",
             "| Hz | Case | Candidate | Delay ms | Pause attenuation dB | Active SNR improvement dB | Active output SNR dB | Offline wall/audio % | p99 block ms | Max block ms | Max > nominal budget |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    for row in report["rows"]:
        def fmt(value):
            return "N/A" if value is None else f"{value:.3f}"
        attenuation = fmt(row["noise_only_attenuation_db"])
        if row["noise_only_output_below_measurement_floor"]:
            attenuation = ">=" + fmt(row["noise_only_attenuation_lower_bound_db"]) + " (below export floor)"
        lines.append("| " + " | ".join((str(row["rate"]), row["case"], row["candidate"], fmt(row["delay_ms"]),
            attenuation, fmt(row["active_snr_improvement_db"]), fmt(row["active_output_snr_db"]),
            fmt(row["offline_cpu_percent"]), fmt(row.get("p99_block_ms")), fmt(row["max_block_ms"]),
            "YES" if row["observed_max_exceeds_nominal_budget"] else "no")) + " |")
    lines += ["", "Clean-case output SNR is unscaled reconstruction quality after a learned fan-noise profile, not a noise-removal score.",
              "", report["timing_note"], "", "The legacy JSON field offline_cpu_percent denotes processing wall/audio %, not sampled CPU utilization.",
              "", "Reel order: " + " -> ".join(report["reel_order"]), "", "## Limits", ""]
    lines += ["- " + limit for limit in report["limits"]]
    (destination / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--speech", type=Path, default=ROOT / "build/noise-lr-release-evidence/audio/speech-original.wav")
    parser.add_argument("--skip-rnnoise", action="store_true", help="Explicitly label RNNoise as untested")
    parser.add_argument("--refresh-report", action="store_true", help="Refresh derived tables from existing JSON; preserve measured timings/audio")
    args = parser.parse_args()
    destination = args.output.resolve(); destination.mkdir(parents=True, exist_ok=True)
    if args.refresh_report:
        report = json.loads((destination / "report.json").read_text(encoding="utf-8"))
        write_report(destination, report)
        print("Refreshed existing evidence presentation; no DSP/timing rerun.")
        return
    probe = ROOT / "build/low-latency-nr/native/Release/stft-nr-probe.exe"
    host = ROOT / "build/tests/directpipe-host-tests_artefacts/Release/directpipe-host-tests.exe"
    source_rate, source_speech = read_wav(args.speech)
    # Match the preceding RNNoise evidence's original speech level, never normalize
    # the individual candidate outputs. This is locally synthesized speech.
    source_speech = source_speech / max(float(np.max(np.abs(source_speech))), 1e-12) * 0.35
    all_rows, manifest = [], {}
    for rate in RATES:
        divisor = math.gcd(source_rate, rate)
        speech = resample_poly(source_speech, rate // divisor, source_rate // divisor)
        for size in (256, 512):
            measured_unity_delay(probe, destination, rate, size)
        for index, kind in enumerate(CASES):
            directory = destination / f"{rate}-{kind}"; directory.mkdir(exist_ok=True)
            clean, noisy, active, quiet = fixture(speech, rate, kind, index)
            for name, data in (("clean", clean), ("input", noisy)):
                wavfile.write(directory / f"{name}.wav", rate, data.astype(np.float32))
            raw = directory / "input.f32"; raw.write_bytes(noisy.astype(np.float32).tobytes())
            for size in (256, 512):
                name = f"stft-{size}"
                target, stats = directory / f"{name}.f32", directory / f"{name}.json"
                run([str(probe), str(raw), str(target), str(stats), str(rate), str(size), "12", "learn", str(rate), "128"])
                row = json.loads(stats.read_text(encoding="utf-8"))
                output = np.fromfile(target, dtype=np.float32).astype(np.float64)
                aligned = output[size:size + len(clean)]
                wavfile.write(directory / f"{name}-delayed.wav", rate, output.astype(np.float32))
                wavfile.write(directory / f"{name}-aligned.wav", rate, aligned.astype(np.float32))
                row.update(metrics(clean, noisy, aligned, active, quiet, rate))
                row.update(candidate=name, case=kind, measured_unity_delay_samples=size,
                           delay_ms=size * 1000 / rate,
                           offline_cpu_percent=row["processing_seconds"] / row["audio_seconds"] * 100)
                all_rows.append(row)
            if not args.skip_rnnoise:
                baseline = directory / "rnnoise-probe"; baseline.mkdir(exist_ok=True)
                # Existing actual-host probe requires eight rates. Feed the matching
                # full case only at the evaluated rate; other rates receive short
                # silence. A new actual processor is constructed for every rate/case.
                for target_rate in (8000, 16000, 32000, 44100, 48000, 88200, 96000, 192000):
                    data = noisy if target_rate == rate else np.zeros(target_rate // 10)
                    wavfile.write(baseline / f"input-{target_rate}.wav", target_rate, data.astype(np.float32))
                env = dict(os.environ, DIRECTPIPE_NOISE_EVIDENCE_DIR=str(baseline))
                run([str(host), "--gtest_filter=BuiltinNoiseRemovalRuntimeEvidence.RenderSpeechAndNoiseAtEightRates",
                     "--gtest_output=json:" + str(baseline / "test-results.json")], baseline / "run.log", env)
                rows = json.loads((baseline / "processing.json").read_text(encoding="utf-8-sig"))
                row = next(x for x in rows if x["rate"] == rate)
                _, output = read_wav(baseline / f"output-{rate}.wav")
                delay = row["reported_latency_samples"]
                aligned = output[delay:delay + len(clean)]
                row.update(metrics(clean, noisy, aligned, active, quiet, rate))
                row.update(candidate="rnnoise-current-host", case=kind, delay_ms=delay * 1000 / rate,
                           offline_cpu_percent=row["processing_seconds"] / row["audio_seconds"] * 100,
                           p99_block_ms=None)
                # Cross-correlation is only a secondary observation: metrics always
                # use declared delay, never the lag that maximizes the score.
                measured = resample_poly(output, 1, 3)
                reference = resample_poly(clean, 1, 3)
                cc = correlate(measured, reference, mode="full", method="fft")
                lags = correlation_lags(len(measured), len(reference))
                valid = (lags >= 0) & (lags < rate / 3 * 0.1)
                row["observed_voice_alignment_ms"] = float(lags[valid][np.argmax(cc[valid])] * 3000 / rate)
                wavfile.write(directory / "rnnoise-aligned.wav", rate, aligned.astype(np.float32))
                all_rows.append(row)
            print(f"Completed {rate} Hz {kind}", flush=True)
    # A short full-scale-preserving listening reel. Each segment is independently
    # delay aligned; there is no output peak/loudness normalization.
    directory = destination / "48000-fan"
    start, end = int(1.5 * 48000), int(9.5 * 48000)
    order = ["clean.wav", "input.wav", "stft-256-aligned.wav", "stft-512-aligned.wav"]
    if not args.skip_rnnoise:
        order.append("rnnoise-aligned.wav")
    reel = []
    for name in order:
        _, data = read_wav(directory / name)
        reel.extend((data[start:end], np.zeros(48000 // 2)))
    wavfile.write(destination / "fan-comparison-reel.wav", 48000, np.concatenate(reel).astype(np.float32))
    hashes = {str(p.relative_to(ROOT)): sha256(p) for p in (
        Path(__file__), Path(__file__).with_name("StftDenoiser.h"), Path(__file__).with_name("probe.cpp"),
        probe, args.speech.resolve(), ROOT / "host/Source/Audio/StreamingStftDenoiser.h",
        ROOT / "host/Source/Audio/BuiltinNoiseRemoval.cpp")}
    if not args.skip_rnnoise:
        hashes[str(host.relative_to(ROOT))] = sha256(host)
    for p in destination.rglob("*.wav"):
        manifest[str(p.relative_to(destination))] = sha256(p)
    report = {
        "protocol": "STFT: manual 1s noise Learn; RNNoise: same 1s noise context with default adaptation/VAD; both: 0.5s settling, held-out evaluation, no gain fit, common clean-derived -35dB RMS activity mask",
        "speech_source": "locally synthesized speech reused from 4.5.1 offline evidence; not user audio; no singing/voice diversity proof",
        "platform": platform.platform(), "python": sys.version, "block_samples": 128,
        "stft_reduction_limit_db": 12, "rnnoise_settings": "current BuiltinNoiseRemoval defaults, Standard VAD 0.70",
        "noise_measurement_floor_rms": 2**-23,
        "sources_sha256": hashes, "audio_sha256": manifest, "rows": all_rows,
        "limits": ["offline software only", "no physical loopback or listening acceptance",
                   "one synthesized voice and synthetic noises", "timings are sequential non-concurrent runs, not hard deadline guarantees",
                   "RNNoise has p99 unavailable; mean/max only", "band level ratios include residual noise outside clean case",
                   "pointwise SNR penalizes waveform/phase differences; it is not a perceptual quality ranking",
                   "noise below common 24-bit one-LSB RMS floor is a lower bound, not a finite measured attenuation",
                   "weak sustained voice may enter the adaptive noise estimate; long-duration robustness is unproven"],
        "reel_order": order,
    }
    write_report(destination, report)
    print(f"Wrote {len(all_rows)} rows: {destination / 'report.md'}")


if __name__ == "__main__":
    main()
