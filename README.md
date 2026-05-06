# Iris Recognition System

A biometric iris recognition system implementing Daugman's IrisCode algorithm, built with C++17 and OpenCV.

## Overview

This system captures, processes, and matches iris images for identity verification. It includes a full client-server architecture with TCP networking, SQL Server database storage, and AES encryption.

## Features

- **Iris Segmentation** — Stable pupil centre detection using heavy Gaussian blur + global minimum, followed by 1D Integro-Differential Operator (IDO) for pupil and iris radii
- **Rubber-Sheet Normalization** — Daugman's concentric mapping from polar to Cartesian coordinates (512×64 strip)
- **Feature Extraction** — Log-Gabor filter bank (4 bands × 4 frequencies, stride 8) → 2048-bit IrisCode
- **Matching** — 64-bit group Hamming distance with ±45° rotational shift compensation (threshold: 0.32)
- **Multi-Template Enrollment** — Up to 3 IrisCode templates per eye stored in SQL Server
- **Security** — AES-256 encryption of biometric data in transit (client ↔ server)
- **Networking** — Async TCP server with thread pool for concurrent client handling

## Architecture

```
client/                  # C++ client application
├── capture/             # Camera capture (OpenCV)
├── network/             # TCP client + AES encryption
├── preprocessing/       # Image quality check, segmentation
└── ui/                  # Display utilities

server/                  # C++ server application
├── iris/                # Core algorithm
│   ├── Normalization    # Segmentation + rubber-sheet mapping
│   ├── FeatureExtractor # CLAHE + Log-Gabor → IrisCode
│   ├── IrisMatcher      # Hamming distance matching
│   └── IrisProcessor    # Enroll / Verify pipeline
├── database/            # SQL Server (ODBC) interface
├── network/             # Async TCP server + connection handler
├── security/            # AES-256 Encryptor
└── utils/               # Logger, ThreadPool

database/
├── schema.sql           # DB schema + stored procedures
└── seed_data.ps1        # Sample data seeding script
```

## Algorithm Pipeline

```
Image → Grayscale → Gaussian Blur (51×51)
      → Pupil Centre (global minimum in central ROI)
      → 1D IDO → Pupil Radius
      → 1D IDO → Iris Radius
      → Rubber-Sheet Normalization (512×64)
      → CLAHE enhancement
      → Log-Gabor filtering (4 scales × 4 orientations)
      → 2048-bit IrisCode + 2048-bit Mask
      → Hamming Distance matching with rotation compensation
```

## Technology Stack

| Component | Technology |
|---|---|
| Language | C++17 |
| Build System | CMake 3.20+ |
| Computer Vision | OpenCV 4.12 |
| Database | SQL Server Express (ODBC Driver 17) |
| Package Manager | vcpkg |
| Compiler | MSVC (Visual Studio 2022) |
| Encryption | AES-256 |

## Results

Tested on the [MMU Iris Database](https://www.unimap.edu.my/index.php/en/ptkpmp/1803):

| Test | Hamming Distance | Result |
|---|---|---|
| Same person, same eye (best) | 0.099 | ✅ MATCH |
| Same person, same eye (worst) | 0.315 | ✅ MATCH |
| Different persons (best separation) | 0.393 | ✅ NO MATCH |

**Decision threshold: 0.32**  
Clear separation between genuine pairs (< 0.32) and impostor pairs (> 0.39).

## Build Instructions

### Prerequisites
- Visual Studio 2022 with C++ workload
- CMake 3.20+
- vcpkg (`C:\vcpkg`)
- SQL Server Express
- OpenCV 4.x (via vcpkg)

### Server

```powershell
cd server
mkdir build2; cd build2
cmake .. -DCMAKE_BUILD_TYPE=Release -DVCPKG_MANIFEST_MODE=OFF `
         -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

### Database Setup

```powershell
sqlcmd -S ".\SQLEXPRESS" -i database\schema.sql
```

## Usage

### Enroll a user

```powershell
.\EnrollTool.exe --enroll --passport <ID> --name "<Name>" --nationality <NAT> `
                 --left <left_eye.bmp> --left2 <left2.bmp> --left3 <left3.bmp> `
                 --right <right_eye.bmp>
```

### Verify identity

```powershell
.\EnrollTool.exe --verify --passport <ID> --eye <left|right> --image <eye.bmp>
```

## Project Structure Notes

- `server/enroll_tool.cpp` — Standalone CLI tool for enrollment and verification (testing/demo)
- `database/schema.sql` — Full schema with `sp_EnrollUser` stored procedure supporting up to 3 templates per eye
- `test_images/` — MMU Iris Database sample images (subfolders 1–46, left/right eyes)

## Author

Final project — Biometric Systems course  
Built with C++17, OpenCV, and SQL Server
