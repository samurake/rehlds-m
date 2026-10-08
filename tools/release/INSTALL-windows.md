## Windows installation: files and destination

Use an existing Windows HLDS/ReHLDS installation. The launcher remains x86;
Windows must be Windows 10 / Server 2016 or newer with its Universal CRT.

| Archive file | Install path |
| --- | --- |
| `hlds.exe` | `<server-root>/hlds.exe` |
| Every bundled x86 CRT `.dll` | `<server-root>/<same DLL filename>` |
| `rehldsm.cfg` | `<server-root>/rehldsm.cfg` |

The server root contains the launcher and game directory, such as `cstrike/`.
Keep these files together; do not install them in `cstrike/`, `addons/`,
`System32/` or `SysWOW64/`. The archive includes only CRT DLLs required by the
launcher and their transitive imports, as listed in `artifact-manifest.json`.
Windows supplies system DLLs and the Universal CRT; do not copy them from
another machine. Engine/game libraries and plugins are not included.

1. Extract to a staging directory. Verify the downloaded archive's `.sha256`
   and its `SHA256SUMS.txt` before installation.
2. Stop the server and disable automatic restart during replacement. Back up
   the launcher, matching runtime paths and any existing cfg; note absent files.
3. Copy the files in the table into the server root together. Preserve existing
   customized cfg values, other libraries, game configuration and plugins.
4. Remove the obsolete `-targetfps` launch option. Configure `target_fps` in
   `rehldsm.cfg`, within 0–2000. The sample uses 1500; 0 selects capped
   pingboost 0–5. For target pacing keep `sys_ticrate 0` and `fps_max 0` in
   `<server-root>/cstrike/server.cfg` (or the selected game's `server.cfg`).
5. Run `hlds.exe --check-config` from a system command prompt before starting
   the normal server entry. Run `rehldsm_version` through console/RCON to
   confirm the loaded launcher. `hlds.exe --version` works without the engine.

All cfg settings load only at process startup. Restart after editing; map
changes and engine `exec` do not reload them. A missing cfg uses built-in
defaults rather than the sample's target/async profile.

Rollback: stop the server, restore backups and old launch options, remove only
paths introduced by this installation, then restart.

Keep manifests, provenance, `SHA256SUMS.txt`, `MSVC-REDIST.txt`, `README.txt`,
`LICENSE` and `licenses/` with the complete staging package. They are
validation/documentation/legal files and are not loaded by the game.
