@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "ATOOLS_DIR=%SCRIPT_DIR%..\.."
set "DATABASE=%ATOOLS_DIR%\..\msfs2024-data\msfs-efb-data\efb_msfs24.sqlite"
set "OUTPUT_DIR=%ATOOLS_DIR%\..\msfs2024-data\msfs2024navdata"
set "EXPORT_SCRIPT=%ATOOLS_DIR%\scripts\export_sqlite_to_json.py"

python "%EXPORT_SCRIPT%" "%DATABASE%" "%OUTPUT_DIR%" --clean

pause
endlocal
