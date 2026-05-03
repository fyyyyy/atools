@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
set "OUTPUT_DIR=%SCRIPT_DIR%..\..\..\msfs2024-data\msfs-efb-data"
set "OUTPUT_DB=%OUTPUT_DIR%\efb_navigraph.sqlite"
set "TEMP_DB=%OUTPUT_DIR%\efb_navigraph_compiling.sqlite"

if "%~1"=="" (
  echo Usage: %~nx0 path\to\navigraph_source.sqlite
  echo.
  echo The source database is the raw Navigraph/DFD database with tables like:
  echo tbl_header, tbl_airports, tbl_runways, tbl_enroute_airways, tbl_iaps.
  echo It is not the already compiled little_navmap_navigraph.sqlite.
  pause
  exit /b 1
)

set "SOURCE_DB=%~1"

if not exist "%SOURCE_DB%" (
  echo Source database does not exist: "%SOURCE_DB%"
  pause
  exit /b 1
)

if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

"%SCRIPT_DIR%navdbbuilder.exe" ^
  --simulator NAVIGRAPH ^
  --source-db "%SOURCE_DB%" ^
  --output "%OUTPUT_DB%" ^
  --temp "%TEMP_DB%" ^
  --force

endlocal
