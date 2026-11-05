"""Summarize a local PCM WAV and host frame log without third-party packages."""
import argparse
import array
import json
import math
from pathlib import Path
import re
import statistics
import wave


def analyze(path, seconds):
    with wave.open(str(path), "rb") as wav:
        channels, width, rate, frames, compression, _ = wav.getparams()
        if compression != "NONE" or width not in (1, 2):
            raise ValueError("Expected uncompressed 8/16-bit PCM")
        result = dict(channels=channels, bits=width*8, rate=rate, frames=frames,
                      duration_seconds=frames/rate, windows=[])
        offset = 0
        energy = count = peak = nonzero = 0
        while raw := wav.readframes(round(seconds*rate)):
            values = array.array("h", raw) if width == 2 else [v-128 for v in raw]
            block_energy = sum(v*v for v in values)
            block_peak = max(map(abs, values), default=0)
            block_nonzero = sum(v != 0 for v in values)
            rms = math.sqrt(block_energy/len(values))
            length = len(values)/channels
            result["windows"].append(dict(start=offset/rate, end=(offset+length)/rate,
                                           rms=rms, peak=block_peak, nonzero=block_nonzero))
            offset += length
            energy += block_energy
            count += len(values)
            nonzero += block_nonzero
            peak = max(peak, block_peak)
        result.update(rms=math.sqrt(energy/count) if count else 0, peak=peak, nonzero_samples=nonzero)
        return result


def frame_summary(path):
    text = path.read_text(encoding="utf-8", errors="replace")
    rows = re.findall(r"frames=(\d+) samples=(\d+) frame_ms mean=([\d.]+) p50=([\d.]+) p95=([\d.]+) max=([\d.]+) step_ms mean=([\d.]+)", text)
    rows = [list(map(float, row)) for row in rows]
    def summary(values):
        if not values:
            return {}
        samples = sum(v[1] for v in values)
        return dict(samples=int(samples), weighted_mean_ms=sum(v[1]*v[2] for v in values)/samples,
                    median_block_p50_ms=statistics.median(v[3] for v in values),
                    median_block_p95_ms=statistics.median(v[4] for v in values),
                    max_block_p95_ms=max(v[4] for v in values), max_frame_ms=max(v[5] for v in values))
    return dict(all=summary(rows), after_frame_720=summary([r for r in rows if r[0]-r[1] >= 720]),
                audio_summary=[line for line in text.splitlines() if "OpenSL summary" in line],
                pause_complete="frame loop stopped; GamePause=1" in text)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("wav", type=Path)
    parser.add_argument("--log", type=Path)
    parser.add_argument("--window-seconds", type=float, default=10)
    args = parser.parse_args()
    if args.window_seconds <= 0:
        parser.error("--window-seconds must be positive")
    result = {"audio": analyze(args.wav, args.window_seconds)}
    if args.log:
        result["frames"] = frame_summary(args.log)
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
