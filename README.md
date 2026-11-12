# Map Editor

Edit PGM/YAML maps and Nav2 keepout or speed masks.

[Web app](https://map-editor-juno.web.app) · [Desktop downloads](https://map-editor-juno.web.app/desktop)

![Editor](docs/screenshot.png)

## Use

Use the **?** button in the web app for a guided practice session with the bundled example map.

1. Open a YAML/PGM pair on desktop, or upload the pair or a ZIP on the web.
2. Choose a layer, then draw, fill, crop, rotate, or adjust the origin.
3. Save with `Ctrl/Cmd+S`. On desktop, `Ctrl/Cmd+Shift+S` creates a new pair. On the web, download a ZIP for local use.

- Use the eyedropper beside the colors to match the original gray.
- Finish polygons with Enter, right-click, or the finish button. Esc cancels.
- Select a mask region and press Delete to remove it.
- Drag crop edges or corners; drag the origin center to move it and its axes to change yaw.
- Dark/light themes and Korean, English, Japanese, and Simplified Chinese are available.

### Masks

Keepout and percentage speed masks are saved as separate PGM/YAML pairs (`mode: raw`).
Values: **0** unrestricted, **100** keepout, **1–100** speed percentage, **255** unknown.
Use Nav2 Filter Info type **0** for keepout or **1** for percentage speed, with base **0** and multiplier **1**.
Standard Nav2 filters require **zero yaw**. The editor preserves the map's coordinates.
On desktop, save modified masks before changing map geometry; on the web, transforms apply to every layer.

### Shared workspaces (web)

Sign in with Google to save maps. Create a workspace from the sidebar and share its link.
Use a map's `⋯` menu to duplicate it, including its masks and settings.
Joining requires administrator approval unless that setting is disabled.
Only one session can edit a shared map at a time; other sessions can view and download it.

| Role | Permissions |
| --- | --- |
| Member | Upload, duplicate, rename, edit, download |
| Workspace admin | Member permissions, manage members, delete/restore maps |
| Service admin | Manage all shared workspaces, delete/restore workspaces, release edit locks |

Personal maps remain private. Deleted maps and workspaces go to trash.

## Desktop

Extract the download for your OS. Qt is bundled; ROS is not required.

| Platform | Launch |
| --- | --- |
| Ubuntu 24.04 x64 | `./MapEditor.sh` |
| Windows x64 | `bin/mapwright.exe` |
| macOS Intel / Apple Silicon | `mapwright.app` |

Packages are unsigned; macOS notarization and APT packages are not provided. Checksums are in `SHA256SUMS`.

Build with C++17, Qt **6.4+**, CMake **3.21+**, and yaml-cpp **0.8+** (fetched automatically if missing):

```bash
# Ubuntu
sudo apt install build-essential cmake ninja-build qt6-base-dev libyaml-cpp-dev
# macOS: brew install cmake ninja qt yaml-cpp
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

Launch `./build/mapwright` on Linux, `build\mapwright.exe` on Windows, or `build/mapwright.app` on macOS.
On Windows, use a Visual Studio C++ developer shell. Set `CMAKE_PREFIX_PATH` to your Qt SDK if needed.
For bundled packages, configure with `-DMAPWRIGHT_DEPLOY_RUNTIME=ON` (Qt 6.7+) and run `cmake --install build --prefix <absolute-path>`.

## Web development

Requires **Node 24.12+**; emulator tests also require **Java 21+**.

```bash
cd web
npm ci
npm run dev
npm test
npm run test:rules
npm run build
# Firebase project administrator login required:
npm run firebase -- login
npm run deploy
```

The configured project is `map-editor-juno` (Google Auth, Firestore, Hosting).
For a different project, update `web/src/firebase.ts` and enable Google sign-in, authorized domains, and a Native Firestore database.
For local authentication, authorize `localhost` / `127.0.0.1`, or run the Auth/Firestore emulators with `VITE_USE_EMULATORS=true`.

Web changes on `main` deploy through GitHub Actions using Workload Identity Federation.
Repository variables: `FIREBASE_WORKLOAD_IDENTITY_PROVIDER` and `FIREBASE_SERVICE_ACCOUNT_EMAIL`.
Desktop builds run on all four platforms; matching `vX.Y.Z` tags publish releases after every build passes.
Update the version in `CMakeLists.txt` before tagging.

## Limits

PGM P2/P5 with Maxval 255; up to **32 Mi pixels**. Web cloud saves allow **20 MiB** per ZIP.
Web files are chunked in Firestore; usage follows Firebase quotas. Private maps are isolated by account.
Desktop overwrites retain backups in `.mapwright-backups`; YAML/PGM replacement cannot be fully atomic across power loss.
Qt/C++ and React/TypeScript currently use separate editing engines with compatible file formats.

[MIT License](LICENSE) · [Third-party licenses](THIRD_PARTY_NOTICES.md)
