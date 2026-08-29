@echo off
rem Install Quota Fabric to a clean prefix and validate an independent downstream consumer.
setlocal
set "PREFIX=%TEMP%\qf_install_clean"
rmdir /s /q "%PREFIX%" 2>nul
cmake --install build --prefix "%PREFIX%"
if errorlevel 1 exit /b 1
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
rmdir /s /q "%TEMP%\qf_consumer_build" 2>nul
cmake -S tests\downstream -B "%TEMP%\qf_consumer_build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%PREFIX%"
if errorlevel 1 exit /b 1
cmake --build "%TEMP%\qf_consumer_build"
if errorlevel 1 exit /b 1
"%TEMP%\qf_consumer_build\consumer.exe"
