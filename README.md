# Map Editor

PGM 이미지와 YAML 메타데이터를 함께 편집하는 Qt 데스크톱 프로그램입니다.
C++17, Qt 6 Widgets, yaml-cpp를 사용합니다. 실행할 때 ROS나 Python은 필요 없습니다.

![편집 화면](docs/screenshot.png)

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

## 사용법

PGM 또는 YAML을 열거나 작업 영역에 끌어 놓습니다. 위쪽 도구 모음에서 도구, 색,
굵기를 고릅니다. 펜과 지우개는 각각 마지막으로 사용한 굵기를 기억합니다(1~512 px).
사각형은 `내부 채움`을 선택할 수 있고, 지우개 색은 회색·흰색·검정 중에서 고릅니다.
채우기는 같은 픽셀값으로 연결된 영역에 적용합니다.

색 버튼의 숫자는 실제 저장할 픽셀값입니다. 기본은 흰색 255, 검정 0, 회색 205이며
회색은 YAML 임계값에서 미확인에 해당하도록 조정됩니다. 원본 회색이 다르면
`I`를 누른 뒤 원본 픽셀을 클릭하세요. 스포이드는 화면 표시색 대신 PGM 값을 가져오고,
펜과 지우개에 같은 값을 적용한 뒤 이전 도구로 돌아갑니다. 회색 옆 `초기화`로 자동값으로
돌아갈 수 있습니다. 원본 픽셀은 열 때 자동 변환하지 않습니다.

오른쪽 `지도 속성`에서 해상도와 원점을 수정합니다. 각도는 도(°)로 입력하며 YAML에는
라디안으로 저장합니다. `점유 설정`을 펼치면 free/occupied 임계값과 negate를 바꿀 수
있습니다. `적용`으로 반영하고 `되돌리기`로 적용 전 변경을 취소합니다. 저장할 때도
미적용 속성을 반영합니다. 속성 패널을 닫았다면 `보기 → 지도 속성`에서 다시 엽니다.

Yaw 수정은 좌표계 방향을 바꿉니다. 픽셀 배열도 돌리려면 `지도 → 이미지 회전`을
사용하세요. 이때 원점과 yaw를 보정해 월드 위치를 유지합니다. 90° 배수가 아닌 회전은
최근접 보간이므로 얇은 선이나 장애물이 달라질 수 있습니다. 회전 후 빈 모서리와
`지도 → 3색으로 변환`의 회색은 YAML 임계값 기준입니다.

## 저장

`저장`은 열린 YAML과 그 파일이 가리키는 PGM을 갱신합니다. `다른 이름으로 저장`은
같은 폴더에 새 이름의 YAML/PGM 쌍을 만들고 `image` 경로도 바꿉니다.
변경 사항이 없는 일반 저장은 파일을 다시 쓰거나 백업을 늘리지 않습니다.

PGM만 열면 기본 해상도 0.05 m/px, 원점 `[0,0,0]`에서 시작하고 저장 시 YAML을 만듭니다.
실제 지도에 맞는 값을 입력하세요. 같은 이름의 YAML이 다른 PGM을 가리키면 오류를 표시합니다.

덮어쓰기 전에는 YAML 폴더의 `.mapwright-backups/`에 원본을 백업합니다.
`original.yaml`, `original.pgm`, 원래 경로를 적은 `paths.txt`가 들어 있습니다.
각 파일은 임시 파일로 작성한 뒤 교체하고, YAML 저장 실패 시 PGM을 복구합니다.
두 파일을 동시에 원자적으로 교체할 수는 없으므로 저장 중 전원 중단 시에는 백업에서
복원해야 합니다. 백업은 자동 삭제하지 않습니다.

열어 둔 파일이 외부에서 바뀌면 일반 저장을 막습니다. 다시 열거나 다른 이름으로 저장하세요.
동시에 다른 프로그램에서 같은 쌍을 저장하는 것은 지원하지 않습니다. 심볼릭 링크에는
덮어쓰지 않습니다.

## 단축키

| 동작 | 키 |
| --- | --- |
| 새 지도 / 열기 / 저장 | Ctrl/Cmd+N / O / S |
| 다른 이름으로 저장 | Ctrl/Cmd+Shift+S |
| 실행 취소 / 다시 실행 | Ctrl/Cmd+Z / Ctrl/Cmd+Shift+Z |
| 펜 / 지우개 / 직선 / 사각형 | B / E / L / R |
| 채우기 / 이동 / 스포이드 | G / H / I |
| 흰색 / 검정 / 회색 | 1 / 2 / 3 |
| 굵기 줄이기 / 늘리기 | [ / ] |
| 화면 맞춤 / 실제 픽셀 크기 | F / 0 |
| 이동 | 가운데 버튼 또는 Space+드래그 |
| 진행 중 획 취소 | Esc |

단일 키는 작업 영역에 포커스가 있을 때만 동작합니다. 적용 전 속성 변경이 있는 상태에서
작업 영역의 실행 취소를 누르면 그 변경부터 취소합니다.

## 지원 범위

- Ubuntu, Windows, macOS. Qt 6.4+, CMake 3.21+, C++17 컴파일러가 필요합니다.
- PGM P2/P5, Maxval 255. 저장은 8-bit P5입니다. 다른 Maxval은 정밀도를 조용히
  줄이지 않고 오류를 표시합니다. 최대 32 Mi 픽셀, YAML 최대 1 MiB입니다.
- YAML의 mode 생략 또는 `trinary`를 지원합니다. `scale`/`raw`는 지원하지 않습니다.
  임계값은 `0 < free_thresh < occupied_thresh < 1`이고 미확인 회색을 표현할 수 있어야 합니다.
- YAML 추가 키·값과 기존 픽셀값을 보존합니다. 실제 변경을 저장하면 YAML/PGM의
  주석과 서식은 재작성됩니다. 손대지 않은 메타데이터의 숫자 정밀도는 유지합니다.
- 실행 취소는 최대 40회이며 압축 기록 누적량이 96 MiB를 넘으면 이전 기록을 비웁니다.
  큰 지도의 채우기·회전은 처리 중 잠시 느려질 수 있습니다.

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

[GitHub Actions](https://github.com/YooJuno/map_editor/actions)에서 OS별 아티팩트를
받고, 그 안의 `.tar.gz`도 풀어 사용합니다. 실행 권한과 Qt 심볼릭 링크를 보존하기 위한
형식입니다. macOS 빌드는 해당 러너 아키텍처용이며 universal binary는 아닙니다.
배포 서명과 notarization은 포함하지 않습니다.

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
`tests/test_mapwright.cpp`에서 파일 왕복, 백업, 좌표, 편집 및 GUI 회귀를 검증합니다.

[PGM 규격](https://netpbm.sourceforge.net/doc/pgm.html) ·
[Nav2 map I/O](https://api.nav2.org/nav2-jazzy/html/map__io_8cpp_source.html) ·
[LICENSE](LICENSE) · [의존성 라이선스](THIRD_PARTY_NOTICES.md)
