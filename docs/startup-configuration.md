# Startup configuration

Place `rehldsm.cfg` beside `hlds_linux` or `hlds.exe`. It is read once, before
logging and engine initialization, independently of the working directory.
Restart the process after edits. Engine `exec` and map changes do not reload it.

## Settings

| Key | Range | Missing-key default | Shipped sample |
| --- | --- | --- | --- |
| `target_fps` | Integer 0–2000 iterations/s | 0 | 1500 |
| `frame_stats_seconds` | Integer 0–3600 seconds | 0 | 60 |
| `async_output` | `true` / `false` | false | true |
| `output_queue_capacity` | Integer 64–4096 slots | 1024 | 1024 |
| `output_flush_ms` | Integer 10–1000 ms | 100 | 100 |

`target_fps > 0` selects absolute-deadline pacing; engine work is part of its
budget. Expired deadlines are skipped without catch-up bursts. Zero selects
`-pingboost` 0–5, or level 0 if omitted. Every mode has a 500 us minimum
iteration period. This limits launcher iterations, not engine cvars or plugin CPU use.

Target pacing is cfg-only. The removed `-targetfps` option fails startup even
with a valid cfg. Keep `fps_max 0` and `sys_ticrate 0` in the game's `server.cfg`;
engine cfg files can override the launcher's inserted `+sys_ticrate 0`.

An explicit `frame_stats_seconds` overrides `-framestats`. Without that cfg key,
`-framestats 1..3600` remains a fallback. Zero in the cfg disables periodic/final
reports; manual `rehldsm_stats` remains available. A missing file uses the
missing-key defaults and does not enable async output.

Syntax is `key = value` with `#` comments; UTF-8 BOM, LF and CRLF are accepted.
Unknown or duplicate keys, malformed lines, NUL bytes, invalid values, unreadable
files and files over 64 KiB fail startup with exit code 2 before engine loading.
Values are rejected, not silently clamped. No shell commands or includes execute.

Validate with `--check-config`. The startup banner reports effective settings
and their sources. `--version` does not require configuration or engine files.

## Output

Async output moves sink formatting and console/optional file I/O to one worker.
Caller-side argument formatting remains on the calling thread. `-condebug`
still controls optional `qconsole.log`; async output does not enable it.

Accepted messages retain FIFO order. Fixed payload slots use about 8 MiB at
capacity 1024 and 32 MiB at 4096. `min(64, capacity/8)` slots are reserved for
warnings/errors. Full queues drop new messages and count them; throwing
destinations are disabled independently while healthy destinations continue.

The worker batches console output into at most 64 KiB, flushes on the configured
timer and handles warning/error flush requests sooner. On Linux, a separate
nonblocking stdout description leaves the original flags unchanged. Pipe/PTY
backpressure retries for at most 20 ms per batch; failed/partial writes count
as failures and can lose the unwritten bytes. SIGPIPE is blocked on this worker.

Fatal flushing waits up to 100 ms; orderly output and metrics-worker shutdown
wait up to 1000 ms each. A timed-out worker retains its owned state until it
finishes or the process exits. Regular-file and Windows device writes can remain
blocked inside the OS. Output is best-effort, not guaranteed archival delivery.

## Commands and metrics

`rehldsm_version` is registered with the engine for console, chaining and RCON.
If registration is unavailable, startup warns and direct launcher input provides
a fallback. `rehldsm_stats` is launcher-input only, not an RCON command.

Metrics use a fixed 2048-iteration ring. A two-slot queue transfers owned snapshots
to the reporter; percentile sorting and formatting occur off the engine thread.

| Fields | Scope |
| --- | --- |
| `frame_us(p50/p95/p99)`, `window_max_frame_us`, `window_over_budget` | Latest 2048 completed launcher iterations, not the full report interval. |
| `engine_call_p99_us` | Engine-call wall time, including callbacks and OS descheduling; not pure engine CPU. |
| `wake_p99_us` | Wake lateness after requested sleeps. |
| `work_p99_us`, `launcher_work_p99_us` | Before-sleep work, and that work minus engine-call wall time. |
| `max_frame_us`, `over_budget_frames` | Process-lifetime recorded-loop maximum and over-budget count. |
| `lifetime_launcher_gt_1ms/5ms/10ms/50ms` | Lifetime counts strictly above each threshold; survive ring rollover and map reloads. |
| `engine_reported_fps` | Engine status value; separate from recorded launcher iterations. |
| `input_high_water`, `input_rejected`, `completion_rejected` | Bounded input/completion queue counters. |
| `output_async`, `output_pending`, `output_high_water`, `output_dropped`, `output_write_failures` | Output state captured at submission. |
| `reports_rejected`, `reports_failed` | Contended/full submissions and worker reporting exceptions. |

Lifetime counters cover completed loop iterations, including sleeps and lifecycle
work. They exclude initialization outside the loop and an unfinished/hung iteration.
Compare deltas between reports; missing reports are not zero failures. Reports
can themselves be dropped by output saturation. External engine-response probes
are needed to check responsiveness and distinguish gameplay from map transitions.

See [implementation](architecture.md) and [qualification](testing.md).
