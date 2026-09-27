## Linux installation: exact files and destination

The destination is the **game server root**: the directory containing
`hlds_linux`, `hlds_run`, and the `cstrike/` folder. The three runtime files
must be beside each other in that directory.

| File from the archive | Action | Destination | Typical permissions |
| --- | --- | --- | --- |
| `hlds_linux` | Replace the existing launcher | `<server-root>/hlds_linux` | `0755` (executable) |
| `libstdc++.so.6` | Replace if present; otherwise add | `<server-root>/libstdc++.so.6` | `0644` (readable) |
| `libgcc_s.so.1` | Replace if present; otherwise add | `<server-root>/libgcc_s.so.1` | `0644` (readable) |

These are the only three files required at runtime from this Linux archive.
They do **not** belong in `cstrike/`, `addons/`, `amxmodx/modules/`, `/lib/`,
or `/usr/lib/`. Keep `hlds_run`, the engine, game DLL, server configuration,
and AMXX/Metamod plugins in place. Do not remove other existing libraries as
part of this launcher installation.

1. Extract the complete archive into a separate staging directory. Keep all
   its files together and run `sh ./check-host.sh` there using a **Linux system
   shell**, or ask the provider to run it. This is not an in-game/RCON console
   command. It checks every packaged file and the launcher dependencies without
   starting the game. The host needs 32-bit glibc >= 2.34, `/lib/ld-linux.so.2`,
   and an SSE4.2 CPU; no glibc or loader file should be uploaded from elsewhere.
2. Stop the server using the hosting panel and ensure automatic restart will
   not start it during replacement. Back up the existing three target files;
   record any that were originally absent.
3. Upload/copy the three runtime files in the table into the game server root.
   Preserve executable permission on `hlds_linux` and readable permission on
   both `.so` files. If FTP rejects replacement or uploaded files disappear,
   have the provider install these exact three files and set their permissions.
4. Start the server through its normal hosting-panel entry or existing
   `hlds_run` wrapper. A full process restart is required; changing map alone
   does not activate a new launcher. Check the version banner and server logs.
5. To roll back, stop the server, restore the original files, remove only a
   runtime file that this installation added to a previously absent path,
   and start the server again.

`check-host.sh`, `SHA256SUMS.txt`, `artifact-manifest.json`, `build-info.json`,
`libgcc-package.txt`, `libstdcpp-package.txt`, `README.txt`, `LICENSE`, and
`licenses/` are validation, provenance, or documentation files. Keep them with
the complete extracted package; the launcher does not load them. Running the
checksum script after copying only the three runtime files will fail because
the other packaged files are absent.

The same launcher supports competitive and ZP servers. This release contains
no named competitive/ZP preset; installation preserves the chosen startup
arguments. Selecting the improved target-FPS pacing is a separate configuration
step using `-targetfps`; see the repository's command-line documentation.
