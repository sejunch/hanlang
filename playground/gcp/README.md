# 한랭 놀이터를 Google Cloud 무료 VM 에 올리기

Google Cloud 의 **e2-micro** VM 은 계속 무료다 (2026년 기준 조건 — 바뀔 수 있으니 가입할 때 확인):
미국 지역(`us-west1`·`us-central1`·`us-east1`) 1대, 표준 디스크 30GB, 북미 밖으로 나가는 데이터 월 1GB.
가입할 때 카드 등록이 필요하다 (무료 범위 안에선 돈이 안 나감).

## 1. VM 만들기 (웹 콘솔, 한 번만)

1. https://console.cloud.google.com 에 가입 → 프로젝트 만들기 → **Compute Engine API 사용**
2. Compute Engine → VM 인스턴스 → **인스턴스 만들기**
   - 리전: `us-west1` (또는 `us-central1`, `us-east1`) — **다른 리전은 무료가 아님**
   - 머신 유형: **e2-micro**
   - 부팅 디스크: **Debian 12**, 디스크 유형 **표준 영구 디스크**, 10~30GB
   - 방화벽: **HTTP 트래픽 허용**, **HTTPS 트래픽 허용** 둘 다 체크
3. 만들어지면 그 줄의 **SSH** 단추 → 브라우저에 터미널이 열림

## 2. 설치 (VM 터미널에서)

```bash
curl -fsSL https://raw.githubusercontent.com/sejunch/hanlang/main/playground/gcp/setup.sh | sudo bash
```

5분쯤 걸리고, 끝나면 `놀이터 주소: https://34-…….sslip.io` 가 나온다. 그 주소로 들어가면 된다.
(sslip.io 는 IP 를 이름으로 바꿔 주는 무료 서비스 — 그 덕에 도메인 없이도 HTTPS 인증서를 받는다)

## 고친 코드 반영

GitHub main 에 푸시한 뒤 VM 에서 같은 명령을 다시 돌리면 최신 코드로 다시 빌드한다.

## 문제가 생기면

```bash
sudo systemctl status hanlang-playground     # 서버 상태
sudo journalctl -u hanlang-playground -n 50   # 서버 기록
sudo journalctl -u caddy -n 50                # HTTPS 인증서 문제
```

## 막아 둔 것

- 컴파일러 `--놀이터`: `실행하다`·낮은 수준(`요청.han` 포함)은 컴파일 오류, 파일은 실행마다 새로 만든 폴더 안만
- 실행 하나: CPU 5초, 8초, 메모리 384MB, 파일 32MB, 동시에 1개 (e2-micro 는 메모리 1GB 라서)
- 서버는 전용 사용자 `hanlang` 으로, 코드 폴더는 못 고치고, 밖으로 인터넷을 못 쓴다 (systemd 로 막음)
