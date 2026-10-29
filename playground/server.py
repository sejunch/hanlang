#!/usr/bin/env python3
"""한랭 웹 놀이터 서버 — 파이썬 표준 라이브러리만 쓴다.

브라우저가 보낸 한랭 코드를 진짜 hanc 로 컴파일해서 돌리고 결과를 돌려준다.
남의 코드를 이 서버에서 돌리는 것이라 막는 장치가 여러 겹이다:
  1. hanc --놀이터 : 실행하다 · 낮은 수준(요청.han 포함) 은 컴파일 오류. 파일은 HAN_PLAYGROUND 를 넘겨서
     런타임이 이 폴더 안만 열게 막는다. 한랭은 범위 검사가 있어서 이러면 서버를 건드릴 길이 없다
  2. 실행 제한 : CPU 5초 · 벽시계 8초 · 메모리 512MB · 파일 32MB (넘으면 운영체제가 끝냄), 보여주는 출력은 1MB 까지
  3. 서버 환경 변수는 안 넘김 (비밀값이 새지 않게), 실행마다 새 임시 폴더
  4. 동시에 2개, IP 마다 1분에 30번

    python3 playground/server.py          # http://localhost:7860
환경 변수: PORT (기본 7860), HANC (기본: 저장소의 ./hanc)
"""
import base64
import json
import os
import resource
import shutil
import signal
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
HANC = os.environ.get("HANC", os.path.join(ROOT, "hanc"))
PORT = int(os.environ.get("PORT", "7860"))

MAX_CODE = 64 * 1024
MAX_INPUT = 64 * 1024
MAX_OUTPUT = 1024 * 1024
RUN_CPU = 5
RUN_WALL = 8
RUN_MEM = 512 * 1024 * 1024
COMPILE_WALL = 30

동시실행 = threading.BoundedSemaphore(2)
요청기록 = {}                     # IP -> 최근 1분 실행 시각들
기록잠금 = threading.Lock()


def 너무잦은가(ip):
    지금 = time.time()
    with 기록잠금:
        최근 = [t for t in 요청기록.get(ip, []) if 지금 - t < 60]
        if len(최근) >= 30:
            요청기록[ip] = 최근
            return True
        최근.append(지금)
        요청기록[ip] = 최근
        return False


def 제한걸기(cpu, mem, 파일크기):
    def 걸다():
        resource.setrlimit(resource.RLIMIT_CPU, (cpu, cpu + 1))
        resource.setrlimit(resource.RLIMIT_AS, (mem, mem))
        resource.setrlimit(resource.RLIMIT_FSIZE, (파일크기, 파일크기))
        resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
        os.setsid()               # 시간 초과면 자식까지 한꺼번에 끝내려고
    return 걸다


def 깨끗한환경(폴더):
    환경 = {"PATH": "/usr/local/bin:/usr/bin:/bin", "LANG": "C.UTF-8", "HOME": 폴더}
    if os.environ.get("DISPLAY"):
        환경["DISPLAY"] = os.environ["DISPLAY"]
    return 환경


def 돌리다(명령, 폴더, 입력, cpu, mem, 벽시계, 파일크기):
    """명령을 돌려 (종료코드, 출력 글, 시간초과?) 를 준다. 출력은 표준 출력·에러를 한 파일에 (순서 그대로)."""
    출력경로 = os.path.join(폴더, "out.txt")
    환경 = 깨끗한환경(폴더)
    환경["HAN_SCREENSHOT"] = os.path.join(폴더, "shot.ppm")
    환경["HAN_PLAYGROUND"] = "1"
    with open(출력경로, "wb") as 출력:
        p = subprocess.Popen(명령, cwd=폴더, stdin=subprocess.PIPE, stdout=출력, stderr=subprocess.STDOUT,
                             env=환경, preexec_fn=제한걸기(cpu, mem, 파일크기))
        시간초과 = False
        try:
            p.communicate(입력.encode("utf-8"), timeout=벽시계)
        except subprocess.TimeoutExpired:
            시간초과 = True
            try:
                os.killpg(p.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            p.wait()
        except BrokenPipeError:
            p.wait()
    with open(출력경로, "rb") as f:
        글 = f.read(MAX_OUTPUT).decode("utf-8", errors="replace")
    if os.path.getsize(출력경로) > MAX_OUTPUT:
        글 += "\n… (출력이 1MB 를 넘어서 잘림)"
    return p.returncode, 글, 시간초과


def 끝난까닭(코드, 시간초과):
    if 시간초과 or 코드 in (-signal.SIGXCPU, -signal.SIGKILL):
        return "시간 초과 (CPU 5초 / 8초까지)"
    if 코드 == -signal.SIGXFSZ:
        return "출력이 너무 많음"
    if 코드 == -signal.SIGSEGV:
        return "프로그램이 죽음 (메모리가 모자랐거나 재귀가 너무 깊음)"
    if 코드 is not None and 코드 < 0:
        return "신호 %d 로 끝남" % -코드
    return ""


def 실행하다(코드, 입력):
    폴더 = tempfile.mkdtemp(prefix="hanlang-")
    try:
        with open(os.path.join(폴더, "놀이터.han"), "w", encoding="utf-8") as f:
            f.write(코드)
        종료, 글, 초과 = 돌리다([HANC, "--놀이터", "놀이터.han", "-o", "prog"], 폴더, "", 30, 2 * 1024 ** 3, COMPILE_WALL,
                            64 * 1024 * 1024)       # 컴파일러가 쓰는 .s·실행파일은 크다
        if 종료 != 0 or not os.path.exists(os.path.join(폴더, "prog")):
            return {"단계": "컴파일", "출력": 글, "종료코드": 종료, "까닭": 끝난까닭(종료, 초과)}
        시작 = time.time()
        종료, 글, 초과 = 돌리다(["./prog"], 폴더, 입력, RUN_CPU, RUN_MEM, RUN_WALL, 32 * 1024 * 1024)
        답 = {"단계": "실행", "출력": 글, "종료코드": 종료, "까닭": 끝난까닭(종료, 초과),
             "걸린시간": round(time.time() - 시작, 3)}
        그림 = os.path.join(폴더, "shot.ppm")
        if os.path.exists(그림) and os.path.getsize(그림) < 30 * 1024 * 1024:
            with open(그림, "rb") as f:
                답["그림"] = base64.b64encode(f.read()).decode("ascii")
        return 답
    finally:
        shutil.rmtree(폴더, ignore_errors=True)


def 예제들():
    목록 = []
    폴더 = os.path.join(ROOT, "examples")
    for 이름 in sorted(os.listdir(폴더)):
        if 이름.endswith(".han"):
            with open(os.path.join(폴더, 이름), encoding="utf-8") as f:
                목록.append({"이름": 이름, "코드": f.read()})
    return 목록


예제캐시 = json.dumps(예제들(), ensure_ascii=False).encode("utf-8")


class 처리기(BaseHTTPRequestHandler):
    server_version = "hanlang-playground"

    def 보내다(self, 상태, 몸, 종류="application/json; charset=utf-8"):
        self.send_response(상태)
        self.send_header("Content-Type", 종류)
        self.send_header("Content-Length", str(len(몸)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(몸)

    def json보내다(self, 상태, 값):
        self.보내다(상태, json.dumps(값, ensure_ascii=False).encode("utf-8"))

    def do_GET(self):
        if self.path in ("/", "/index.html"):
            with open(os.path.join(HERE, "index.html"), "rb") as f:
                self.보내다(200, f.read(), "text/html; charset=utf-8")
        elif self.path == "/examples.json":
            self.보내다(200, 예제캐시)
        else:
            self.보내다(404, b"not found", "text/plain")

    def do_POST(self):
        if self.path != "/run":
            return self.보내다(404, b"not found", "text/plain")
        길이 = int(self.headers.get("Content-Length") or 0)
        if 길이 > MAX_CODE + MAX_INPUT + 1024:
            return self.json보내다(413, {"오류": "코드가 너무 김 (64KB까지)"})
        try:
            요청 = json.loads(self.rfile.read(길이).decode("utf-8"))
            코드 = str(요청.get("코드", ""))
            입력 = str(요청.get("입력", ""))
        except (ValueError, UnicodeDecodeError):
            return self.json보내다(400, {"오류": "요청 형식이 잘못됨"})
        if len(코드.encode("utf-8")) > MAX_CODE or len(입력.encode("utf-8")) > MAX_INPUT:
            return self.json보내다(413, {"오류": "코드나 입력이 너무 김 (64KB까지)"})
        ip = self.headers.get("X-Forwarded-For", self.client_address[0]).split(",")[0].strip()
        if 너무잦은가(ip):
            return self.json보내다(429, {"오류": "너무 자주 돌림 — 1분에 30번까지"})
        if not 동시실행.acquire(timeout=15):
            return self.json보내다(503, {"오류": "다른 사람 코드를 돌리는 중 — 잠시 뒤 다시"})
        try:
            self.json보내다(200, 실행하다(코드, 입력))
        finally:
            동시실행.release()

    def log_message(self, 형식, *값들):
        sys.stderr.write("%s %s\n" % (self.log_date_time_string(), 형식 % 값들))


def 가상화면띄우다():
    """창 그리기는 그림 저장 모드에서도 X 화면이 있어야 해서, 없으면 Xvfb 를 하나 띄운다."""
    if os.environ.get("DISPLAY") or not shutil.which("Xvfb"):
        return
    subprocess.Popen(["Xvfb", ":99", "-screen", "0", "1280x1024x24", "-nolisten", "tcp"],
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    os.environ["DISPLAY"] = ":99"
    time.sleep(0.5)


if __name__ == "__main__":
    가상화면띄우다()
    서버 = ThreadingHTTPServer(("0.0.0.0", PORT), 처리기)
    print("한랭 놀이터: http://localhost:%d" % PORT, flush=True)
    서버.serve_forever()
