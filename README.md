# MyLightroom

Desktop RAW photo editor inspired by Lightroom Classic, with **AI histogram matching** to transfer a reference look across different RAW files (Canon CR2/CR3 and more via LibRaw).

## Features

- Non-destructive develop pipeline (Basic, Tone, Curve, HSL, Color Grading, Calibration, LUT, Effects, Detail)
- AI Match: edit one reference RAW, apply consistent look to batch via histogram fingerprinting
- Presets: full XMP import (Basic, Tone Curve, HSL, Color Grading, Calibration, Effects, Detail)
- LUT import: `.cube` and `.3dl` with intensity slider; auto-link companion LUT when importing XMP bundle
- Library catalog (SQLite), filmstrip, history/snapshots
- Dark Lightroom-like UI (Qt 6)

## Requirements

- Windows 10/11 (primary target), CMake 3.24+, MSVC 2022 or MinGW
- Qt 6.7+ (Widgets, OpenGLWidgets, Sql)
- LibRaw, SQLite3, nlohmann-json
- Optional: ONNX Runtime (CUDA) for ML parameter refinement

## Build (Windows + vcpkg + Visual Studio 2026)

This project includes vcpkg under `tools/vcpkg` (no admin required on `C:\`).

```powershell
cd "d:\Project\!MyLightroom"

# Configure (installs qtbase + libraw from vcpkg.json — first run may take 30–90 min)
cmake -B build -S . `
  -DCMAKE_TOOLCHAIN_FILE="d:/Project/!MyLightroom/tools/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -G "Visual Studio 18 2026" -A x64 `
  -DMYLR_BUILD_TESTS=OFF -DMYLR_USE_ONNX=OFF

cmake --build build --config Release
.\build\Release\MyLightroom.exe
```

If you use **Visual Studio 2022** instead, replace the generator with `"Visual Studio 17 2022"`.

## Build (Windows + vcpkg + MSVC)

Install [Visual Studio 2022](https://visualstudio.microsoft.com/) with "Desktop development with C++" and [vcpkg](https://vcpkg.io):

```powershell
git clone https://github.com/microsoft/vcpkg C:\vcpkg
C:\vcpkg\bootstrap-vcpkg.bat
C:\vcpkg\vcpkg install qt6-base qt6-widgets qt6-opengl libraw sqlite3 --triplet x64-windows

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Optional ONNX (CUDA):

```powershell
C:\vcpkg\vcpkg install onnxruntime[cuda]:x64-windows
cmake -B build -S . -DMYLR_USE_ONNX=ON ...
```

## Build (vcpkg)

```powershell
git clone https://github.com/microsoft/vcpkg
.\vcpkg\bootstrap-vcpkg.bat
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=path\to\vcpkg\scripts\buildsystems\vcpkg.cmake
cmake --build build --config Release
```

## Run

```powershell
.\build\Release\MyLightroom.exe
```

Open a folder of RAW files via **File → Import Folder** or drag `.cr2` files onto the window.

## Sidecar format

Edits are stored as `{filename}.cr2.mylr` JSON next to each RAW file.

## AI Match workflow

1. Edit reference image in Develop module
2. **Match → Save Reference Profile**
3. Select other images → **Match → Apply to Selected**
