@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "OUTPUT_DIR=%SCRIPT_DIR%..\..\..\msfs-data"
set "OUTPUT_DB=%OUTPUT_DIR%\little_navmap_msfs24.sqlite"
set "BASE_PATH=%LOCALAPPDATA%\Packages\Microsoft.Limitless_8wekyb3d8bbwe\LocalState"
set "SIMCONNECT_DLL=%SCRIPT_DIR%SimConnect.dll"

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

"%SCRIPT_DIR%navdbbuilder.exe" ^
  --simulator MSFS24 ^
  --output "%OUTPUT_DB%" ^
  --basepath "%BASE_PATH%" ^
  --simconnect-dll "%SIMCONNECT_DLL%" ^
  --language en-US ^
  --read-addon-xml ^
  --force

endlocal
