## Linux installation: files and destination

Stop the server before replacement. Install in the **server root** containing
`hlds_run` and the game directory (for example `cstrike/`), not inside the game
or `addons/` directories.

| Archive file | Install path | Permissions |
| --- | --- | --- |
| `hlds_linux` | `<server-root>/hlds_linux` | `0755` |
| `libstdc++.so.6` | `<server-root>/libstdc++.so.6` | `0644` |
| `libgcc_s.so.1` | `<server-root>/libgcc_s.so.1` | `0644` |
| `rehldsm.cfg` | `<server-root>/rehldsm.cfg` | `0644` |

The first three are required launcher runtime files. The cfg is required for
the documented target/async profile, but the launcher can start without it
using built-in defaults. The sample uses `target_fps = 1500`, not 2000.
Preserve existing custom values; do not blindly overwrite an installed cfg.

1. Extract the complete archive to a separate staging directory. Run
   `sh ./check-host.sh` using a Linux system shell there, or ask the provider
   to run it. This verifies package checksums, dependencies and immediate
   symbol binding without starting the engine; it is not an RCON command.
2. The host must supply **32-bit glibc 2.34+**, `/lib/ld-linux.so.2` and an
   SSE4.2 CPU. Do not install packaged files in `/lib/` or `/usr/lib/`, and
   never replace the host's glibc or loader with files from another machine.
3. Stop the server and disable panel auto-restart during replacement. Back up
   the launcher, both runtime paths and any existing cfg; record absent files.
4. Copy the files in the table into the server root and set permissions.
   Leave `hlds_run`, engine/game libraries, other existing libraries and
   Metamod/AMXX plugins in place. This archive does not replace them.
5. Remove the obsolete `-targetfps` launch option. Set `target_fps` in the cfg,
   within 0–2000, and keep `sys_ticrate 0` and `fps_max 0` in
   `<server-root>/cstrike/server.cfg` (or the selected game's `server.cfg`)
   when using target pacing. `target_fps = 0` selects capped pingboost 0–5.
6. Validate with `./hlds_linux --check-config`, then start through the normal
   panel entry or `hlds_run`. Verify the banner and run `rehldsm_version`
   in console/RCON. Every cfg setting loads only at process startup; a map
   change or engine `exec` does not reload it.

Rollback: stop the server, restore the backed-up files/cfg, remove only paths
introduced by this installation, restore old launch options and restart.

`check-host.sh`, `SHA256SUMS.txt`, the manifests/build/package provenance,
`README.txt`, `LICENSE` and `licenses/` are validation/documentation/legal
files, not game runtime files. Keep the complete staging archive for checks
and rollback. The checksum script needs all archived files, not just the
four copied to the server root. No test executable, instrumentation, engine,
plugin, glibc, loader or Intel runtime is included.
