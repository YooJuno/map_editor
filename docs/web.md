# 웹 버전

[웹 앱 열기](https://map-editor-juno.web.app) · [README](../README.md) · [Firebase 프로젝트](https://console.firebase.google.com/project/map-editor-juno/overview)

React·TypeScript·Canvas 기반 웹 편집기입니다. 데스크톱 앱과 같은 저장소의 `web/`에 있습니다.

- Google 로그인 시 자동 가입, 계정별 지도 목록
- PGM + YAML 또는 ZIP 열기, 이름 변경, 클라우드 저장, ZIP 다운로드
- 지도 편집, 스포이드, 진입 금지·속도 제한 마스크, 구역별 삭제
- 회전·자르기·원점 드래그, 실행 취소·다시 실행
- 휴지통과 복원, 다크·라이트 테마, 한국어·영어·일본어·중국어

## 로컬 실행

Node.js **24.12 이상**이 필요합니다.

```bash
cd web
npm ci
npm run dev
```

`http://127.0.0.1:5173`에서 파일을 열어 바로 편집할 수 있습니다. 로그인 없이 편집·다운로드가
가능하며, 클라우드 저장은 Google 로그인이 필요합니다.

## Firebase 초기 설정과 배포

보내준 웹 앱 설정은 이미 코드에 반영되어 있습니다. 실제 서비스 활성화와 배포에는
`map-editor-juno` 프로젝트의 관리자 권한이 필요합니다.

1. Firebase Console → **Authentication → Sign-in method → Google**을 활성화하고 지원 이메일을 선택합니다.
2. **Firestore Database**에서 `(default)` 데이터베이스를 **Standard / Native mode**로 만듭니다.
   기존 데이터베이스가 있으면 그대로 사용합니다. 새 데이터베이스의 위치는 실제 사용자 지역에 맞게 선택합니다.
3. Authentication → Settings → Authorized domains에서 배포 주소를 확인합니다.
   기본 주소는 `map-editor-juno.web.app`, `map-editor-juno.firebaseapp.com`입니다.
   로컬에서 Google 로그인을 테스트하려면 `localhost`와 `127.0.0.1`도 등록합니다.
4. 저장소의 `web/`에서 실행합니다.

```bash
npm run firebase -- login
npm run deploy
```

호스팅 파일과 Firestore 보안 규칙·인덱스를 함께 배포합니다. 프로젝트에 다른 앱이 있다면
`firestore.rules`의 적용 범위를 먼저 합쳐야 합니다. 이 규칙은 이 앱의 경로 외 접근을 허용하지 않습니다.
배포 완료 후 주소: `https://map-editor-juno.web.app`

배포용 비공개 서비스 계정 키는 소스나 웹 앱 설정에 넣지 않습니다.
전달받은 Firebase 웹 API 키는 브라우저 연결용이며 배포 자격 증명이 아닙니다.

## GitHub 자동 배포

`.github/workflows/web.yml`이 웹 변경을 빌드하고 파일 변환·UI·보안 규칙을 테스트합니다.

`main`의 웹 변경이 검증을 통과하면 Hosting과 Firestore 규칙·인덱스를 자동 배포합니다.
Google Workload Identity Federation으로 GitHub의 짧은 수명 인증을 사용합니다.
`YooJuno/map_editor` 저장소의 `main`만 배포할 수 있으며 서비스 계정 비공개 키는 만들지 않습니다.

저장소 Actions variables:

- `FIREBASE_WORKLOAD_IDENTITY_PROVIDER`: 배포용 Google OIDC provider 리소스 이름
- `FIREBASE_SERVICE_ACCOUNT_EMAIL`: `map-editor-deploy@map-editor-juno.iam.gserviceaccount.com`

배포 계정에는 이 프로젝트의 Hosting 배포, Firebase 보안 규칙 배포, Firestore 인덱스 관리와
프로젝트 설정 조회 권한만 부여합니다. 지도 데이터의 직접 읽기·쓰기 권한은 부여하지 않습니다.

## 저장 방식

Hosting, Authentication, Firestore를 사용합니다. Cloud Storage는 사용하지 않습니다.
지도와 마스크를 ZIP으로 압축하고 600,000바이트 이하의 청크로 나눠 계정별 경로에 저장합니다.

```text
users/{uid}/maps/{mapId}                  # 목록·현재 리비전·미리보기
users/{uid}/maps/{mapId}/payloads/{chunk} # 압축한 파일 데이터
```

파일을 전부 올린 뒤 트랜잭션으로 현재 리비전을 교체합니다. 실패한 업로드는 기존 지도를 바꾸지 않으며,
다른 창에서 먼저 저장한 경우 덮어쓰기를 막습니다. 직전 리비전 하나를 유지하고 이전 청크는 정리합니다.
업로드 도중 창을 강제 종료하면 게시되지 않은 청크가 남을 수 있습니다. 휴지통은 복원할 수 있으며
현재 웹 앱에는 영구 삭제 기능이 없습니다.

PGM은 P2/P5·Maxval 255, 한 변 최대 16,384 px·총 32 Mi 픽셀을 지원합니다.
클라우드 저장은 압축 후 **20 MiB**까지입니다. 다운로드 ZIP에는 원본과 마스크 각각의 PGM/YAML 쌍이
들어 있습니다. ZIP은 웹에서 다시 열거나 압축을 풀어 데스크톱 앱에서 사용할 수 있습니다.
PGM만 열면 0.05 m/px, 원점 `[0, 0, 0]`에서 시작합니다.

회전·자르기·좌표 변경은 현재 연 마스크에도 함께 적용해 격자를 맞춥니다.
원점 Yaw가 0이 아니면 표준 Nav2 필터의 호환성 안내를 표시합니다.
페이지를 닫기 전 저장하거나 ZIP으로 내려받아야 하며, 저장하지 않은 변경에는 닫기 확인이 표시됩니다.
Firestore 사용량은 [프로젝트별 무료 한도와 요금](https://firebase.google.com/docs/firestore/quotas)을 따릅니다.

## 검증

```bash
npm test
npm run build
# Java 21 이상
npm run test:rules
```

마지막 명령은 데모 프로젝트의 로컬 Auth·Firestore 에뮬레이터만 사용합니다.
Google 테스트 계정의 저장·재열기·이름 변경·휴지통·복원과 다른 계정의 접근 차단을 검증합니다.
실제 Google OAuth 화면과 운영 배포는 관리자 로그인 후 별도로 확인해야 합니다.

브라우저에서도 에뮬레이터를 사용하려면 터미널 두 개에서 실행합니다.

```bash
npm run firebase -- emulators:start --project demo-map-editor --only auth,firestore
VITE_USE_EMULATORS=true npm run dev
```

Windows PowerShell에서는 `$env:VITE_USE_EMULATORS="true"; npm run dev`를 사용합니다.
에뮬레이터를 지정한 개발 빌드는 운영 프로젝트 대신 `demo-map-editor`를 사용합니다.
