# 빌드·배포

[README](../README.md) · [사용 가이드](usage.md)

C++17, Qt 6.4+, CMake 3.21+가 필요합니다. 명령은 저장소 루트에서 실행합니다.

## 실행

이미 빌드했다면:

```bash
./run.sh
./run.sh examples/office.yaml
```

Ubuntu 24.04에서 처음 빌드하려면:

```bash
sudo apt install build-essential cmake ninja-build qt6-base-dev libyaml-cpp-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/mapwright
```

한글 글꼴이 없으면 `fonts-noto-cjk`를 설치하세요. 사용자 경로에 설치된 `ctest`가
동작하지 않는 환경에서는 `/usr/bin/ctest`를 사용할 수 있습니다.

## Windows / macOS 빌드

Windows는 Visual Studio 2022 C++ 도구, CMake, Git, Qt SDK를 설치한 뒤 x64 개발자
명령 프롬프트에서 빌드합니다(Qt 6.8.3 예시).

```bat
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.8.3/msvc2022_64
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build\mapwright.exe
```

직접 실행 시 Qt `bin`이 PATH에 있어야 합니다. Visual Studio 생성기를 사용한다면
Release 구성을 선택하고 `build\Release\mapwright.exe`를 실행하세요.

macOS는 Xcode Command Line Tools와 Homebrew 환경에서:

```bash
brew install cmake ninja qt yaml-cpp
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
ctest --test-dir build --output-on-failure
open build/mapwright.app
```

yaml-cpp 0.8 이상이 설치되어 있지 않으면 CMake가 고정된 0.8.0 소스를 받아 빌드합니다.
오프라인 빌드는 의존성을 먼저 설치하고 `MAPWRIGHT_FETCH_YAML=OFF`로 설정하세요.

## 실행 패키지

[Releases](https://github.com/YooJuno/map_editor/releases/latest)에서 OS에 맞는 파일을
내려받아 압축을 풀어 사용합니다. Qt 라이브러리와 예제 지도를 포함합니다.
앱 창·작업 표시줄과 Windows 실행 파일·macOS 앱 번들에 전용 아이콘을 포함합니다.
Linux를 CMake로 설치하면 앱 메뉴용 아이콘도 함께 설치합니다.

| 환경 | 배포 파일 | 실행 |
| --- | --- | --- |
| Windows x64 | `MapEditor-버전-windows-x86_64.zip` | `bin/mapwright.exe` |
| Ubuntu 24.04 x64 | `MapEditor-버전-ubuntu-24.04-x86_64.tar.gz` | `./MapEditor.sh` |
| Apple Silicon Mac | `MapEditor-버전-macos-arm64.tar.gz` | `mapwright.app` |
| Intel Mac | `MapEditor-버전-macos-x86_64.tar.gz` | `mapwright.app` |

Linux/macOS의 `.tar.gz`는 실행 권한과 Qt 링크를 보존합니다. `SHA256SUMS`에서 다운로드한
파일의 SHA-256 값을 확인할 수 있습니다. 배포 서명과 macOS notarization은 포함하지 않습니다.

현재는 APT 저장소나 `.deb`를 제공하지 않으므로 `sudo apt install mapwright`로 설치할 수 없습니다.

## 자동 빌드·배포

`main`에 푸시하거나 PR을 열면 네 환경에서 빌드·테스트·패키징합니다.
버전 태그를 푸시하면 모두 성공한 뒤 GitHub Release를 자동으로 게시합니다.
직접 저장하는 토큰 없이 Actions의 `GITHUB_TOKEN`을 사용합니다.

새 버전을 배포할 때는 `CMakeLists.txt`의 `project(... VERSION ...)`을 바꾸고 커밋한 뒤:

```bash
git push origin main
git tag -a v1.2.0 -m "Map Editor 1.2.0"
git push origin v1.2.0
```

태그와 앱 버전이 다르면 배포하지 않습니다. 실패한 실행은 Actions에서 다시 실행할 수 있고,
이미 완성된 Release를 다시 실행하면 기존 배포 파일을 덮어쓰지 않습니다.
태그 없이 실행한 빌드는 [Actions](https://github.com/YooJuno/map_editor/actions)의 아티팩트로
확인할 수 있습니다. Actions에서 직접 실행하려면 `Build, test and release`를 선택하세요.

## 직접 실행 패키지 만들기

직접 Qt 런타임을 묶으려면 Qt 6.7 이상에서:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMAPWRIGHT_DEPLOY_RUNTIME=ON
cmake --build build --parallel
cmake --install build --prefix "$PWD/dist"
```

배포 경로는 절대 경로로 지정하세요. Windows 명령 프롬프트에서는 `--prefix "%CD%/dist"`를
사용합니다. Windows/Linux는 `dist/bin/`, macOS는 `dist/mapwright.app`에 실행 파일이
있습니다. Linux는 빌드한 배포판의 glibc와 시스템 GUI 라이브러리 호환성이 필요합니다.

## 소스

`src/mapio.*`는 파일 입출력과 좌표 변환, `src/document.*`는 편집 상태와 실행 취소,
`src/canvas.*`는 그리기와 화면 조작, `src/mainwindow.*`는 UI를 담당합니다.
`src/workqueue.*`는 재사용하는 작업 스레드와 작업 중 조작 제한을 담당합니다.
`tests/`에서 파일 왕복, 백업, 좌표, 마스크, 편집, 비동기 응답과 GUI 회귀를 검증합니다.
`src/i18n.*`와 `resources/i18n.json`에 번역을 내장하므로 별도 번역 패키지나 네트워크가 필요 없습니다.
성능 비교와 점검 내용은 [v1.7.0](audit-v1.7.0.md), [v1.7.1 추가 최적화](audit-v1.7.1.md)에 있습니다.
`tests/test_i18n.cpp`는 언어 전환 중 편집 상태 보존을, `tests/test_i18n_catalog.py`는 번역 누락과
치환 변수 일치를 검사합니다.

앱 아이콘 원본은 `resources/mapwright.svg`입니다. PNG·ICO·ICNS를 다시 만들려면 개발 환경에
`scripts/icon-requirements.txt`를 설치한 뒤 `python scripts/generate_icons.py`를 실행합니다.
아이콘 생성용 Python 라이브러리는 앱 실행과 일반 빌드에 필요하지 않습니다.
CI는 Windows의 실제 실행 파일 리소스, macOS 번들·ICNS 디코딩, Linux 설치 경로를 확인합니다.

[PGM 규격](https://netpbm.sourceforge.net/doc/pgm.html) ·
[Nav2 map I/O](https://api.nav2.org/nav2-jazzy/html/map__io_8cpp_source.html) ·
[LICENSE](../LICENSE) · [의존성 라이선스](../THIRD_PARTY_NOTICES.md)
