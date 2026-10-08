@echo off
setlocal
pushd "%~dp0..\.."
if not defined BUILD_JOBS set BUILD_JOBS=2
if not defined BUILD_ROOT set "BUILD_ROOT=%CD%\out\windows-build"
if not defined OUTPUT_ROOT set "OUTPUT_ROOT=%CD%\out\ci"
if not exist "%OUTPUT_ROOT%" mkdir "%OUTPUT_ROOT%"
"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -version "[17.0,18.0)" -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%OUTPUT_ROOT%\vs-install.txt"
if errorlevel 1 exit /b 1
for /f "usebackq delims=" %%i in ("%OUTPUT_ROOT%\vs-install.txt") do set "VS_INSTALL=%%i"
if not defined VS_INSTALL exit /b 1
call "%VS_INSTALL%\VC\Auxiliary\Build\vcvarsamd64_x86.bat"
if errorlevel 1 exit /b 1
cmake -S . -B "%BUILD_ROOT%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_UNIT_TESTS=ON -DENABLE_EXCEPTIONS=ON -DOPTIMIZE_FOR_CURRENT_CPU=OFF -DBIN_OUTPUT_DIR="%OUTPUT_ROOT%\windows-i386"
if errorlevel 1 exit /b 1
cmake --build "%BUILD_ROOT%" --parallel %BUILD_JOBS%
if errorlevel 1 exit /b 1
cmake -E copy_if_different data/rehldsm.cfg "%OUTPUT_ROOT%\windows-i386\rehldsm.cfg"
if errorlevel 1 exit /b 1
python tools/linux/build-provenance.py "%BUILD_ROOT%" "%OUTPUT_ROOT%\windows-i386\build-provenance.json"
if errorlevel 1 exit /b 1
python tools/windows/runtime.py --destination "%OUTPUT_ROOT%\windows-i386"
if errorlevel 1 exit /b 1
ctest --test-dir "%BUILD_ROOT%" --output-on-failure --parallel %BUILD_JOBS% --output-junit "%OUTPUT_ROOT%\tests.xml"
exit /b %errorlevel%
