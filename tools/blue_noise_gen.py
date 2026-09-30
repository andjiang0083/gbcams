#!/usr/bin/env python3
"""Generate 64x64 blue-noise dither matrix — filtered-white-noise + rank.

Practical blue-noise approximation (no numpy needed):
  1. White noise field (uniform random 0..1)
  2. Gaussian blur (torus-wrapped) — suppresses low-frequency content
  3. Rank-normalize: replace each value by its percentile rank → uniform
     0..63 distribution with blue-noise-ish spectral character

This is the standard "filtered noise ranked" approach used for dither
arrays when a full void-and-cluster is overkill. Good enough for
GBC-2's organic grain without Bayer checkerboard artifacts.

Output: C array of 4096 uint8 values (0-63) for ESP32 GBC-2 filter.
"""
import random, sys

N = 64
SEED = 42
SIGMA = 1.6          # blur radius in pixels (tune: larger = finer grain)
rng = random.Random(SEED)

def gaussian_kernel(sigma):
    r = max(1, int(3 * sigma))
    k = []
    for dy in range(-r, r + 1):
        row = []
        for dx in range(-r, r + 1):
            row.append((1.0 / (2 * 3.14159265 * sigma * sigma)) *
                       (2.718281828 ** (-(dx * dx + dy * dy) / (2 * sigma * sigma))))
        k.append(row)
    return k, r

def main():
    # 1. white noise
    noise = [[rng.random() for _ in range(N)] for _ in range(N)]
    # 2. gaussian blur (torus)
    kern, r = gaussian_kernel(SIGMA)
    blurred = [[0.0] * N for _ in range(N)]
    for i in range(N):
        for j in range(N):
            acc = 0.0
            for ky, dy in enumerate(range(-r, r + 1)):
                for kx, dx in enumerate(range(-r, r + 1)):
                    acc += noise[(i + dy) % N][(j + dx) % N] * kern[ky][kx]
            blurred[i][j] = acc
    # 3. rank-normalize to 0..63
    flat = sorted(v for row in blurred for v in row)
    vals = [[0] * N for _ in range(N)]
    for i in range(N):
        for j in range(N):
            # percentile rank: number of values strictly below, / (N²-1), * 63
            import bisect
            vals[i][j] = bisect.bisect_left(flat, blurred[i][j]) * 63 // (N * N - 1)
    # emit
    print(f"// 64x64 blue-noise dither LUT (filtered-white-noise + rank, toroidal)")
    print(f"// sigma={SIGMA} seed={SEED} — values 0-63, uniform distribution")
    print(f"// use: thr = s_blue64[y & 63][x & 63]  (per-channel offset for decorrelation)")
    print("static const uint8_t s_blue64[64][64] = {")
    for i in range(N):
        row = ", ".join(f"{vals[i][j]:2d}" for j in range(N))
        print(f"  {{ {row} }},")
    print("};")
    # stats
    import collections
    c = collections.Counter(v for row in vals for v in row)
    print(f"// sanity: min={min(c)} max={max(c)} unique={len(c)} (expect 0,63,64)",
          file=sys.stderr)

if __name__ == "__main__":
    main()
