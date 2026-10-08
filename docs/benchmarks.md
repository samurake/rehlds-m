# Launcher measurements

These are historical local measurements, not guarantees for a hosting provider.
They use the same supplied engine/game/plugins, not a stock engine comparison.
Raw logs and fixture settings stay private; manifests and numeric reports are
retained under the evidence paths below.

## Pacing mechanism

September's synthetic test requested 1000 iterations/s for 1000 iterations on
one logical CPU. Injected work ran inside a fake engine call.

| Engine work/iteration | Original loop iterations/s | Absolute-deadline loop |
| --- | ---: | ---: |
| 0 us | 1015.53 | 999.92 |
| 250 us | 809.71 | 999.91 |
| 750 us | 575.62 | 999.92 |

The original loop slept after engine work instead of accounting for it in the
budget. These results isolate that defect, not gameplay performance.
Evidence: `out/modernization-20260927/`.

An October synthetic output test used 250 us engine work and a simulated 300 us
write per iteration. At target 2000, synchronous output measured 1582.59
iterations/s, versus 1999.71 with async output. This compares two output paths
in the same launcher, not RC2 versus 1.4.0. No output was dropped in those cases.
Evidence: `out/startup-config-20261008/frame-benchmark-single-cpu.jsonl`.

## Three-launcher combat comparison

October 8's cfg-only build campaign used three independent runs per cell,
eight seconds warmup and 30 seconds measured with scripted engine fake clients.
Runs were serial on logical CPU 2, with 1 GiB RAM and no CPU quota/network.
The shared host was not exclusively reserved. Values below are run medians.

| Players | Launcher | Accepted FPS | CPU/core | p99 gap ms |
| ---: | --- | ---: | ---: | ---: |
| 10 | Classic, sys_ticrate 2000 | 869.2 | 7.1% | 1.951 |
| 10 | Published RC2, target 2000 | 1990.9 | 10.5% | 0.664 |
| 10 | Cfg-only build, target 2000 | 1981.2 | 10.4% | 0.683 |
| 32 | Classic, sys_ticrate 2000 | 813.2 | 13.2% | 3.050 |
| 32 | Published RC2, target 2000 | 1904.0 | 17.5% | 1.672 |
| 32 | Cfg-only build, target 2000 | 1919.9 | 17.0% | 1.411 |

The cfg-only build did not improve both populations: 32-player medians were
better, but 10-player FPS/p99 were worse. Gameplay hit rates varied, and compiler,
fmt and launcher changes were combined. These runs do not isolate any one change
or establish statistical significance. The default async queue adds about 8 MiB.
Evidence: `out/modernization-20261008/player-load/`.

The earlier cfg-only comparison is retained in
`out/launcher-three-way-2000-20261008/`; its 32-player medians were 1904.2 FPS
for the cfg-only build and 1911.9 for RC2. It also did not establish a broad speedup.

## Lifetime-counter qualification

Five fresh processes per population/stage used 60-second warmups and 120-second
measurements at target 2000. The revised build added lifetime stall counters,
not a pacing change. Stages ran in chronological batches, not interleaved.

| Players | Stage | Median FPS | p99 / p99.9 gap ms | Total gaps >10 ms |
| ---: | --- | ---: | ---: | ---: |
| 10 | Before counters | 1989.4 | 0.668 / 1.270 | 30 |
| 10 | With counters | 1981.3 | 0.671 / 1.301 | 3 |
| 32 | Before counters | 1906.4 | 1.473 / 3.787 | 39 |
| 32 | With counters | 1915.0 | 1.437 / 3.648 | 3 |

All 20 runs passed gameplay/shutdown checks; no measured gap exceeded 50 ms.
Ranges overlap, so the reduced long-gap count cannot be attributed to counters.
Four further 32-player windows in one monitored process passed three reloads,
34 engine-response heartbeats and 137 resource samples. Equivalent-window RSS
medians were 173.7, 176.6, 174.0 and 174.1 MiB, with 22 threads and 42 descriptors.
This short lifecycle check does not establish a leak-free long-term plateau.
Evidence: `out/candidate-stability-20261008/`.

The dated narrative reports were consolidated into this page and
[testing](testing.md). Original copies remain locally in
`out/release-1.4.0-20261008/docs-before-cleanup/`; raw campaign evidence is unchanged.
