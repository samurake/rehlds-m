# ReHLDS-M

**English** | **[Русский](README.ru.md)**

This fork develops ReHLDS-M **1.4.0**, based on [hun1er/rehlds-m](https://github.com/hun1er/rehlds-m).
[Download builds](https://github.com/samurake/rehlds-m/releases) · [Build pipeline](https://github.com/samurake/rehlds-m/actions/workflows/build-and-test.yml)

---

Rehlds-m is a modern, cross-platform launcher for the Half-Life Dedicated Server (HLDS), written from scratch in C++20. It acts as a replacement for the standard `hlds.exe` executable, providing enhanced performance, stability, and a host of new features for server administrators, while maintaining full compatibility with the original engine libraries (`swds.dll` on Windows, `engine_i486.so` on Linux) and game mods.

## Key Features & Differences from original HLDS/ReHLDS

- **Modern Codebase:** Written in C++20 using modern practices and the Standard Template Library (STL).
- **Performance:** Compiled for modern CPUs with SSE4.2 instruction set support for maximum efficiency.
- **Enhanced Console:**
  - **Advanced Input:** Full support for `HOME`, `END`, and `DELETE` keys for easier command editing.
  - **Command History:** The last 100 commands are saved to `input_history.txt` and loaded on startup. Navigation with arrow keys is fully supported.
  - **Improved Status Bar (Windows):** The server status bar is no longer tied to the first line and is always visible.
- **Full UTF-8 Support:** Ensures correct display of all characters without requiring OEM conversions on Windows.
- **New Performance Modes:**
  - Added `-pingboost` levels 4 and 5 for more aggressive performance tuning on both Windows and Linux.
  - Introduced the `-targetfps` parameter for adaptive real-time FPS management.
- **New Utility Parameters:**
  - `-conclearlog`: Clears the `qconsole.log` file on server start.
  - `-ignoresigint`: Prevents the server from shutting down on a `CTRL+C` keypress.
  - `-pidfile`: Saves the server's process ID to a specified file.
- **Stability:**
  - Fixes for several bugs present in the original ReHLDS.
  - Advanced memory dump creation on Windows for easier crash diagnostics.
  - Uses Winsock 2.2 on Windows (up from 2.0).

## Requirements

- **Operating System:**
  - Windows x86. This checkout is built and tested with Visual Studio 2022; older Windows releases need separate runtime qualification.
  - Linux i386 with the runtime symbols listed in the build's `artifact-manifest.json`. The current AlmaLinux builder produces a package requiring symbols through GLIBC_2.35; do not infer compatibility from the distribution name alone.
- **CPU:** A modern processor with support for the **SSE4.2** instruction set.
- **Dependencies (Windows):** [Microsoft Visual C++ Redistributable 2015-2022 (x86)](https://aka.ms/vs/17/release/vc_redist.x86.exe) must be installed.

## Installation

1.  **Backup:** Make a backup of your original server executable (e.g., `hlds.exe` on Windows, or `hlds_linux`/`hlds_run` on Linux).
2.  **Replace:** Copy all files from the release archive into your server's root directory.
    -   For **Windows**, this includes `hlds.exe`.
    -   For **Linux**, this includes `hlds_linux` and several library files (`lib*.so`). Ensure the main executable has execute permissions (`chmod +x hlds_linux`).
3.  **Launch:** Start your server using the new executable as you normally would, with any desired command-line parameters.

## Command-Line Parameters

This launcher supports all standard HLDS parameters. Below are the new or significantly modified parameters introduced by rehlds-m.

### Performance Tuning

#### `-pingboost <level>`

This parameter adjusts the server's sleep policy to control CPU usage and server FPS.

-   **`0`**: (Default) Standard 1ms sleep. Minimal CPU usage.
-   **`1`**: (Linux only) Uses `setitimer` for a 1ms sleep cycle.
-   **`2`**: (Linux only) Uses `poll` instead of the legacy `select` function for a 1ms sleep.
-   **`3`**: The original engine's implementation (`NET_Sleep_Timeout`). Behavior is unchanged.
-   **`4`**: (New) A more aggressive boost. The CPU sleeps for a shorter duration (1 microsecond), resulting in higher FPS and responsiveness. Available on both Linux and Windows.
-   **`5`**: (New) Maximum performance mode. The server thread yields instead of sleeping, aiming for the highest possible FPS. This will cause one CPU core to run at 100% load.

#### `-targetfps <value>`

This uses monotonic, absolute deadlines to pace launcher iterations. Engine work counts toward each frame's time budget; the launcher sleeps only for the remaining time. Overruns skip expired deadlines without a catch-up loop.

-   **Usage:** `-targetfps 1000`
-   Valid values are integers from `1` to `10000`. Invalid values stop startup with an error.
-   The launcher supplies `+sys_ticrate 0` and removes `-pingboost` when this mode is selected. Keep `sys_ticrate 0` in server configuration too, since configuration files can override the command line.
-   This cannot make engine or plugin work fit a budget it already exceeds. Launcher iterations and the engine's reported FPS are separate measurements.

#### `-framestats <seconds>` and `rehldsm_stats`

`-framestats 10` writes a report every ten seconds and at orderly loop exit. Valid intervals are `1` to `3600`; periodic reporting is disabled when omitted. Enter `rehldsm_stats` in the launcher console for an immediate report. This is a launcher command and is not registered as an engine/RCON command.

Reports include launcher frame p50/p95/p99, engine-call p99, wake lateness, lifetime maximum frame time, over-budget frames and queue saturation. Percentiles use the latest 2048 iterations. Collection uses fixed storage; reporting adds a small sorting/logging cost. Map-load and shutdown work can appear in the lifetime maximum.

Console commands are limited to 1022 bytes, with 128 queued commands and four submitted per iteration. Oversized lines are rejected in full. A standalone `quit` has a separate slot so a full queue cannot block shutdown. Completion requests have a 250 ms timeout and are cancelled during shutdown. EOF disables the input reader while the server continues running headless.

### Utility Parameters

#### `-conclearlog`

If present, this parameter will clear the contents of the `qconsole.log` file every time the server starts.

#### `-ignoresigint`

When used, this prevents the server from shutting down when `CTRL+C` is pressed in the console. The server can then only be shut down using the `quit` or `exit` command.

#### `-pidfile <filename>`

Writes the current process ID (PID) of the server to the specified file.
- **Usage:** `-pidfile "hlds.pid"`

## Building from Source

To compile the project yourself, you will need:

-   [Git](https://git-scm.com/)
-   [CMake](https://cmake.org/) (version 3.21 or higher)
-   A C++20 compatible compiler (e.g., Visual Studio 2022, GCC 11+, Clang with C++20 thread support)

```bash
# 1. Clone the repository
git clone https://github.com/samurake/rehlds-m.git
cd rehlds-m

# 2. Configure the project with CMake
cmake -B build

# 3. Build the project
cmake --build build --config Release
```
The compiled binary is placed under `bin/<compiler>-<version>-<configuration>/` unless `BIN_OUTPUT_DIR` is supplied. Run `hlds_linux --version` or `hlds.exe --version` to print its version and source revision without loading the engine.

For the qualified Linux container workflow, sanitizer profile, runtime manifests and benchmark, see [tools/linux](tools/linux/README.md). Implementation and local validation results are recorded in [the modernization report](docs/modernization-20260927.md).

### GitHub builds and releases

Every branch push and pull request builds Linux i386 and Windows x86, runs tests,
and runs a separate Linux ASan/UBSan job. Successful release builds upload archives
and SHA-256 files to the workflow run; validation evidence is retained for 30 days.
Manual runs are also supported with `workflow_dispatch`.

Pushing a matching version tag, such as `v1.4.0-rc.1`, runs the same checks and
publishes both platform archives to GitHub Releases only after all jobs pass.
Tags with a suffix are prereleases; plain `v1.4.0` is a stable release. The tag
must match `CMakeLists.txt`, and version-specific notes must exist in
`docs/releases/<version>.md`. Published releases are not overwritten by reruns.

Archives include version/commit identity, runtime requirements, licenses and
checksums. Linux tar archives preserve the launcher's executable permission.
The pipeline uses GitHub's built-in token; it requires no FTP credentials and
does not install builds on game servers. See [release instructions](tools/release/README.md).

### Advanced Build Options

#### Optimizing for the Server's CPU

You can compile the launcher with optimizations tailored for the specific processor where the server will run. This enables the use of additional instruction sets (beyond the required SSE4.2), which can result in an FPS boost.

To enable this option, use the `-DOPTIMIZE_FOR_CURRENT_CPU=ON` flag in step 2 of the example above:

```bash
# 2. Configure the project with CMake, optimizing for the current machine's CPU
cmake -B build -DOPTIMIZE_FOR_CURRENT_CPU=ON
```

**Warning:** Use this flag only if you are building the launcher on the same machine where it will be used.

## F.A.Q.

-   **Q: UTF-8 characters are not displaying correctly in the console.**
    -   **A:** Change your console font to one that supports UTF-8 characters, such as `Consolas` or `Lucida Console`.

-   **Q: The server won't start on Windows, complaining about a missing `msvcp140.dll`.**
    -   **A:** You need to install the [Microsoft Visual C++ Redistributable 2015-2022 (x86)](https://aka.ms/vs/17/release/vc_redist.x86.exe).

-   **Q: The status bar is not visible on Windows 11.**
    -   **A:** Go to Settings -> Privacy & Security -> For developers -> Terminal, and change the default terminal application to "Windows Console Host".

## License

This project is licensed under the terms of the provided `LICENSE` file. It also utilizes components of the Half-Life 1 SDK, which is subject to the license agreement from Valve Corporation.
