@echo off
setlocal
pushd "%~dp0..\.."
if not defined BUILD_JOBS set BUILD_JOBS=2
if not exist out\ci mkdir out\ci
"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > out\ci\vs-install.txt
if errorlevel 1 exit /b 1
for /f "usebackq delims=" %%i in ("out\ci\vs-install.txt") do set "VS_INSTALL=%%i"
if not defined VS_INSTALL exit /b 1
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if errorlevel 1 exit /b 1
cmake -S . -B out/windows-build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_UNIT_TESTS=ON -DOPTIMIZE_FOR_CURRENT_CPU=OFF -DBIN_OUTPUT_DIR="%CD%\out\ci\windows-i386"
if errorlevel 1 exit /b 1
cmake --build out/windows-build --parallel %BUILD_JOBS%
if errorlevel 1 exit /b 1
python tools/windows/runtime.py
if errorlevel 1 exit /b 1
ctest --test-dir out/windows-build --output-on-failure --parallel %BUILD_JOBS% --output-junit "%CD%\out\ci\tests.xml"
exit /b %errorlevel%
