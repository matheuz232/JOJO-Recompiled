# Building JOJO Recompiled on Windows

## Requirements

- Windows 10 or Windows 11 x64
- Visual Studio 2022 with **Desktop development with C++**
- MSVC v143 x64/x86 build tools
- Windows 10/11 SDK
- CMake 3.20+

## Build

Open **Developer PowerShell for VS 2022** in the repository root:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected end-user application:

```text
build\Release\JOJO-Recompiled.exe
```

`jojo_tests.exe` is a developer test binary and is not part of an end-user package.

## First launch

1. Open `JOJO-Recompiled.exe`.
2. Select a legally obtained image of your own copy (`.iso`, `.bin`, `.cue`, `.gdi`).
3. Click **PREPARAR JOGO**.
4. The progress bar reflects real conversion stages and writes `conversion.log` under `%LOCALAPPDATA%\JOJO Recompiled\game\logs`.

At the current milestone the preparation foundation completes, but the application reports that the game-specific native backend is still pending. That is a development state, not an installation error.
