# navdbbuilder

`navdbbuilder` is a small command-line wrapper around `atools::fs::NavDatabase`.
It follows the Little Navmap scenery library load flow: open a temporary SQLite
database, create the schema, run the compiler, and rename the temporary database
to the final output after a successful load.

Build it with CMake:

```bat
cmake -S . -B build-navdbbuilder -G "Visual Studio 17 2022" -A x64 ^
  -DATOOLS_BUILD_NAVDBBUILDER=ON ^
  -DCMAKE_PREFIX_PATH=C:\Qt\6.5.3\msvc2019_64 ^
  -DATOOLS_SIMCONNECT_PATH_WIN64_MSFS_2024="C:\MSFS 2024 SDK\SimConnect SDK"
cmake --build build-navdbbuilder --target navdbbuilder --config Release
```

Run it from a shell where Qt's runtime DLLs are on `PATH`:

```bat
set PATH=C:\Qt\6.5.3\msvc2019_64\bin;%PATH%
```

Minimal MSFS 2024 usage:

```bat
build-navdbbuilder\Release\navdbbuilder.exe ^
  --simulator MSFS24 ^
  --output little_navmap_msfs24.sqlite ^
  --basepath "%LOCALAPPDATA%\Packages\Microsoft.Limitless_8wekyb3d8bbwe\LocalState" ^
  --simconnect-dll "C:\MSFS 2024 SDK\SimConnect SDK\lib\SimConnect.dll" ^
  --force
```

Pass `--config path\to\navdatareader.cfg` to reuse Little Navmap-style
`NavDatabaseOptions`. Use `--schema-only` to create an empty database schema
without loading simulator data.

The executable waits for Enter before closing by default. Add `--no-pause` for
scripts and automated tests.
