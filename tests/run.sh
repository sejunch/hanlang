#!/usr/bin/env bash
# 한랭 회귀 테스트 전부 돌리기.  사용:  make test   또는  ./tests/run.sh
cd "$(dirname "$0")/.." || exit 1
ROOT=$PWD

pass=0; fail=0
# 창 그리기는 창을 띄우는 대신 그림을 파일로 받는다 (테스트가 창 닫기를 기다리며 멈추지 않게)
export HAN_SCREENSHOT="$(mktemp -u)".ppm
HAS_X=0; [ -n "$DISPLAY" ] && HAS_X=1
tmp=$(mktemp -d); trap 'rm -rf "$tmp"' EXIT

ok()   { pass=$((pass+1)); }
bad()  { fail=$((fail+1)); echo "  실패: $1"; }

# 잘 돌아야 하는 것들 (종료코드 0)
run_ok() {
    local f="$1"; shift
    if printf '%s' "$*" | timeout 20 ./han "$f" >/dev/null 2>"$tmp/err"; then ok
    else bad "$f"; sed 's/^/    /' "$tmp/err" | head -3; fi
}

# 실행 중 오류로 종료코드 1 이 나야 하는 것들
run_dies() {
    local f="$1"
    if ./hanc "$f" -o "$tmp/p" >/dev/null 2>&1 && ! "$tmp/p" >/dev/null 2>&1; then ok
    else bad "$f (실행 오류가 나야 정상)"; fi
}

# 컴파일 에러가 나야 하는 것들
run_rejects() {
    local f="$1"
    if ./hanc "$f" -o "$tmp/p" >/dev/null 2>&1; then bad "$f (컴파일 에러가 나야 정상)"
    else ok; fi
}

echo "== 데모"
run_ok examples/demo.han
run_ok examples/demo2.han "$(printf '세준\n17\n')"
run_ok examples/demo3.han "$(printf '3\n10\n15\n2.5\n')"
run_ok examples/demo4.han
run_ok examples/demo5.han
run_ok examples/demo6.han "$(printf '17 25\n5\n42 7 99 13 68\n')"
run_ok examples/demo7.han
run_ok examples/demo8.han

echo "== 기능"
for f in tests/t_float.han tests/t_func.han tests/t_edge.han tests/t_list.han \
         tests/t_builtin.han tests/t_str.han tests/t_lib.han tests/t_mok.han \
         tests/t_poly.han tests/t_strcmp.han tests/t_grid.han tests/t_loop.han \
         tests/t_split.han tests/t_end.han tests/t_bit.han tests/t_code.han \
         tests/t_struct.han tests/t_lib2.han tests/t_sortlib.han \
         tests/t_statlib.han tests/t_gridlib.han tests/t_fraclib.han tests/t_biglib.han tests/t_pop.han \
         tests/t_tree.han tests/t_nest.han tests/t_dict.han \
         tests/t_v18.han tests/t_heaplib.han tests/t_timelib.han tests/t_relib.han; do
    run_ok "$f"
done
run_ok tests/t_eof.han "$(printf '10\n20\n12\n')"
# 파일을 만드는 테스트는 임시 폴더 안에서 돌리고, 결과 파일 내용까지 본다
if ./hanc tests/t_file.han -o "$tmp/tf" 2>"$tmp/err" && (cd "$tmp" && ./tf >/dev/null) &&
   [ "$(cat "$tmp/t_file_copy.txt")" = "$(printf '> 첫째 줄\n> 42\n> 3.5 / 끝\n> 덧붙인 줄')" ]; then ok
else bad "tests/t_file.han"; sed 's/^/    /' "$tmp/err" | head -3; fi

# 못 열면 / (빈 X 목록): 파일을 만들어서 임시 폴더에서 돌린다
if ./hanc tests/t_v15.han -o "$tmp/t15" 2>"$tmp/err" &&
   [ "$(cd "$tmp" && ./t15)" = "$(printf '못 열었음\n계속 감\n한 줄\n한 줄\n(없음)\n0\n가\n1\n2\n0')" ]; then ok
else bad "tests/t_v15.han"; sed 's/^/    /' "$tmp/err" | head -3; fi
# 환경값·실제경로·실행파일·실행하다 (v17)
if ./hanc tests/t_v17.han -o "$tmp/t17" 2>"$tmp/err" &&
   [ "$(HAN_V17=있음 "$tmp/t17")" = "$(printf '있음\n1\n1\n1\n1\n3\n바깥에서 찍음\n0')" ]; then ok
else bad "tests/t_v17.han"; sed 's/^/    /' "$tmp/err" | head -3; fi
run_ok tests/t_mlstr.han
# 표.han: CSV 를 만들어 읽고 쓰므로 임시 폴더에서
if ./hanc tests/t_tablelib.han -o "$tmp/ttab" 2>"$tmp/err" && out=$(cd "$tmp" && ./ttab) &&
   [ "$(echo "$out" | sed -n '1p;9p;15p;16p;17p;18p;19p')" = "$(printf '5\n86\n180\n3\n민수\n3\n과일,값')" ] &&
   [ "$(echo "$out" | tail -1)" = '사과,"1,000"' ]; then ok
else bad "tests/t_tablelib.han"; echo "$out" | head -3 | sed 's/^/    /'; fi
# 창 그리기: 화면(X)이 있을 때만. 그림 파일 크기·머리만 본다 (모양은 셀프 컴파일러와 픽셀 비교)
if [ $HAS_X = 1 ]; then
    if ./hanc tests/t_win.han -o "$tmp/tw" 2>"$tmp/err" && [ "$("$tmp/tw")" = "창 닫힘" ] &&
       [ "$(head -c 15 "$HAN_SCREENSHOT")" = "$(printf 'P6\n320 200\n255\n')" ]; then ok
    else bad "tests/t_win.han"; sed 's/^/    /' "$tmp/err" | head -3; fi
    # 실시간 창: HAN_FRAMES 번째 다시그리다 에서 그림을 저장하고 창닫힘 이 참이 된다
    if ./hanc tests/t_win2.han -o "$tmp/tw2" 2>"$tmp/err" &&
       [ "$(HAN_FRAMES=3 "$tmp/tw2")" = "다시그리다 3번, 키 0개" ] &&
       [ "$(head -c 15 "$HAN_SCREENSHOT")" = "$(printf 'P6\n200 100\n255\n')" ]; then ok
    else bad "tests/t_win2.han"; sed 's/^/    /' "$tmp/err" | head -3; fi
    # 3D: lib/입체.han 의 두 그리기 (삼각형그리다 / 깊이 버퍼 + 그림으로그리다)
    if ./hanc tests/t_win3.han -o "$tmp/tw3" 2>"$tmp/err" &&
       [ "$(HAN_FRAMES=2 "$tmp/tw3")" = "$(printf '380\n그림 둘')" ] &&
       [ "$(head -c 15 "$HAN_SCREENSHOT")" = "$(printf 'P6\n160 120\n255\n')" ]; then ok
    else bad "tests/t_win3.han"; sed 's/^/    /' "$tmp/err" | head -3; fi
    # 마우스: 창 없이 돌리면 (0, 0) · 안 눌림, 같은 이름 변수가 먼저
    if ./hanc tests/t_win4.han -o "$tmp/tw4" 2>"$tmp/err" &&
       [ "$("$tmp/tw4")" = "$(printf '(0, 0)\n0\n0\n안 눌림\n7')" ] &&
       [ "$(head -c 14 "$HAN_SCREENSHOT")" = "$(printf 'P6\n120 80\n255\n')" ]; then ok
    else bad "tests/t_win4.han"; sed 's/^/    /' "$tmp/err" | head -3; fi
    # 늘어나게 연 창 + (창크기): 창 없이 돌리면 연 크기 그대로
    if ./hanc tests/t_win5.han -o "$tmp/tw5" 2>"$tmp/err" &&
       [ "$("$tmp/tw5")" = "$(printf '(150, 90)\n13500\n(150, 90)\n3')" ] &&
       [ "$(head -c 14 "$HAN_SCREENSHOT")" = "$(printf 'P6\n150 90\n255\n')" ]; then ok
    else bad "tests/t_win5.han"; sed 's/^/    /' "$tmp/err" | head -3; fi
    # 누르고 있는 키: 창 없이 돌리면 아무것도 안 눌림, 누른키들 은 변수 이름으로도
    if ./hanc tests/t_win6.han -o "$tmp/tw6" 2>"$tmp/err" &&
       [ "$("$tmp/tw6")" = "$(printf '0\n0\n안 눌림\n2')" ]; then ok
    else bad "tests/t_win6.han"; sed 's/^/    /' "$tmp/err" | head -3; fi
    if ./hanc examples/게임/상자모으기.han -o "$tmp/game3" 2>"$tmp/err" &&
       [ "$(HAN_SEED=1 HAN_FRAMES=2 "$tmp/game3")" = "모은 상자: 0" ]; then ok
    else bad "examples/게임/상자모으기.han"; sed 's/^/    /' "$tmp/err" | head -3; fi
    # 창 게임 예제: 컴파일되고 몇 프레임 돌다 끝나는지
    if ./hanc examples/게임/모양채우기.han -o "$tmp/game" 2>"$tmp/err" &&
       [ "$(HAN_SEED=1 HAN_FRAMES=3 "$tmp/game")" = "점수: 0" ]; then ok
    else bad "examples/게임/모양채우기.han"; sed 's/^/    /' "$tmp/err" | head -3; fi
else echo "  (화면이 없어서 창 그리기 테스트는 건너뜀)"; fi
# 경고: 컴파일은 되면서 표준 에러에 [경고] 가 나와야 하는 것
for f in tests/e32.han tests/e33.han; do
    if ./hanc "$f" -o "$tmp/p" 2>&1 | grep -q '^\[경고\]'; then ok; else bad "$f ([경고] 가 나와야 함)"; fi
done
# (명령줄)·종료하다·표준 에러: 화면 출력, 에러 출력, 종료코드를 따로따로 본다
if ./hanc tests/t_args.han -o "$tmp/ta" 2>"$tmp/err"; then
    out=$("$tmp/ta" 가나 12 2>"$tmp/stderr"); rc=$?
    if [ $rc -eq 3 ] && [ "$out" = "$(printf '2\n가나\n13\n다시 화면으로')" ] &&
       [ "$(cat "$tmp/stderr")" = "$(printf '이건 표준 에러로\n끝냅니다')" ]; then ok
    else bad "tests/t_args.han (rc=$rc)"; fi
else bad "tests/t_args.han"; sed 's/^/    /' "$tmp/err" | head -3; fi
if ./hanc tests/t_exit0.han -o "$tmp/te" 2>"$tmp/err" && [ "$(cd "$tmp" && ./te)" = "0" ] &&
   [ "$(cat "$tmp/t_exit0_out.txt")" = "파일로 감" ]; then ok
else bad "tests/t_exit0.han"; fi

echo "== 실행 오류 (종료코드 1 이 정답)"
for f in tests/t_div0.han tests/t_mod0.han tests/t_oob.han tests/t_gerr.han tests/t_shift.han tests/t_popempty.han \
         tests/t_none.han tests/t_nofile.han tests/t_dictmiss.han tests/t_rebad.han; do
    run_dies "$f"
done

echo "== 쓰레기 수거"
# 메모리를 200MB 로 묶고 돌린다. 수거가 안 되면 400MB 넘게 써서 죽는다.
if ./hanc tests/t_gc.han -o "$tmp/gc" 2>"$tmp/err" &&
   out=$( (ulimit -v 200000; "$tmp/gc") 2>&1 ) &&
   [ "$out" = "$(printf '300\n1286\n1\n150000\n1286')" ]; then ok
else bad "tests/t_gc.han (메모리 제한 안에서 끝나야 하고 출력이 맞아야 함)"; echo "$out" | head -3 | sed 's/^/    /'; fi
# 할당할 때마다 수거하는 모드로 기능 테스트를 다시 돌려 출력이 보통 모드와 똑같은지 본다.
# 살아 있는 값을 잘못 치우면 여기서 깨진 값·죽음으로 드러난다.
# 보통 모드에선 수거가 거의 안 일어나서 못 잡는 버그들이다. (무작위 쓰는 테스트는 뺐다)
for f in examples/demo4.han examples/demo7.han tests/t_thread2.han tests/t_list.han tests/t_str.han \
         tests/t_struct.han tests/t_split.han tests/t_pop.han tests/t_grid.han \
         tests/t_poly.han tests/t_lib.han tests/t_sortlib.han tests/t_fraclib.han \
         tests/t_biglib.han tests/t_statlib.han tests/t_gridlib.han tests/t_strcmp.han \
         tests/t_tree.han tests/t_nest.han tests/t_dict.han tests/t_dictcache.han; do
    if ./hanc "$f" -o "$tmp/p" 2>"$tmp/err" &&
       a=$("$tmp/p" </dev/null 2>&1) && b=$(HAN_GC_STRESS=1 timeout 60 "$tmp/p" </dev/null 2>&1) &&
       [ "$a" = "$b" ]; then ok
    else bad "$f (수거 스트레스 모드에서 결과가 달라짐)"; fi
done

echo "== 정답 비교 (출력·종료코드·에러 메시지를 tests/expected/ 와 글자 단위로)"
# 정답은 C++ 로 짠 옛 컴파일러(hanc.cpp, 2026-10-28 지움)가 낸 결과다. 한랭으로 짠 지금 컴파일러가
# 그것과 똑같이 돌아야 한다. 프로그램은 임시 폴더에서 같은 입력·인자로 돌린다.
# 새 기능을 넣어 출력이 바뀌는 게 맞으면: 눈으로 확인한 뒤 tests/expected/ 의 그 파일을 새로 쓰기.
export HAN_SEED=1          # (N의 무작위) 를 쓰는 프로그램도 결과가 늘 같게
# t_req 은 libcurl 이 있을 때만 (lib/요청.han 이 -lcurl 로 링크)
skip='t_win\|t_gpu'                   # 창·GPU 테스트는 따로 (화면·장치가 있을 때만)
ldconfig -p 2>/dev/null | grep -q 'libcurl\.so' || skip='t_win\|t_gpu\|t_req'
for f in $(ls examples/*.han tests/t_*.han | grep -v "$skip"); do
    exp="tests/expected/$(dirname "$f" | tr / _)_$(basename "$f" .han).out"
    if [ ! -f "$exp" ]; then bad "$f (정답 파일 $exp 이 없음)"; continue; fi
    if ./hanc "$f" -o "$tmp/gp" 2>"$tmp/err" &&
       (cd "$tmp" && rm -f t_*.txt t_*.csv; printf '10\n20\n12\n' | ./gp 가나 12 > out 2>&1; echo "rc=$?" >> out) &&
       cmp -s "$tmp/out" "$exp"; then ok
    else bad "$f (출력이 정답과 다름)"; diff "$tmp/out" "$exp" 2>/dev/null | head -4 | sed 's/^/    /'; fi
done
for f in tests/e*.han; do
    ./hanc "$f" -o "$tmp/gp" >/dev/null 2>"$tmp/err"
    if cmp -s "$tmp/err" "tests/expected/err_$(basename "$f" .han).out"; then ok
    else bad "$f (에러 메시지가 정답과 다름)"; diff "$tmp/err" "tests/expected/err_$(basename "$f" .han).out" | head -4 | sed 's/^/    /'; fi
done
unset HAN_SEED

echo "== GPU (Vulkan) — 장치가 있을 때만"
if ldconfig -p 2>/dev/null | grep -q 'libvulkan\.so' && ./hanc tests/t_gpu.han -o "$tmp/tg" 2>"$tmp/err"; then
    out=$("$tmp/tg" 2>&1)
    if echo "$out" | grep -q 'GPU 를 쓸 수 없음'; then echo "  (Vulkan 장치가 없어서 GPU 테스트는 건너뜀)"
    else
        echo "$out" > "$tmp/out"; echo "rc=0" >> "$tmp/out"
        if cmp -s "$tmp/out" tests/expected/tests_t_gpu.out; then ok
        else bad "tests/t_gpu.han (GPU 답이 정답과 다름)"; diff "$tmp/out" tests/expected/tests_t_gpu.out | head -4 | sed 's/^/    /'; fi
        # 자료 목록(넷째 인자) + GPU32.han
        if ./hanc tests/t_gpu2.han -o "$tmp/tg2" 2>"$tmp/err" && { "$tmp/tg2"; echo "rc=$?"; } > "$tmp/out" 2>&1 &&
           cmp -s "$tmp/out" tests/expected/tests_t_gpu2.out; then ok
        else bad "tests/t_gpu2.han (GPU 자료·GPU32 답이 정답과 다름)"; diff "$tmp/out" tests/expected/tests_t_gpu2.out | head -4 | sed 's/^/    /'; fi
        # 벡터 (GPU 안에서 평면·공간 벡터)
        if ./hanc tests/t_gpu3.han -o "$tmp/tg3" 2>"$tmp/err" && { "$tmp/tg3"; echo "rc=$?"; } > "$tmp/out" 2>&1 &&
           cmp -s "$tmp/out" tests/expected/tests_t_gpu3.out; then ok
        else bad "tests/t_gpu3.han (GPU 벡터 답이 정답과 다름)"; diff "$tmp/out" tests/expected/tests_t_gpu3.out | head -4 | sed 's/^/    /'; fi
        if [ $HAS_X = 1 ]; then
            # GPU 레이 트레이싱 3D 게임: 몇 프레임 돌다 끝나는지
            if ./hanc examples/게임/구슬굴리기.han -o "$tmp/marble" 2>"$tmp/err" &&
               [ "$(HAN_SEED=1 HAN_FRAMES=3 "$tmp/marble")" = "점수: 0" ]; then ok
            else bad "examples/게임/구슬굴리기.han"; sed 's/^/    /' "$tmp/err" | head -3; fi
            # 레이 트레이서 예제 (창): 작게 64비트·32비트로 한 장씩 그려 봄 (창 대신 PPM)
            for m in 64 32; do
                if ./hanc examples/GPU/광선추적.han -o "$tmp/gpurt" 2>"$tmp/err" &&
                   [ "$(HAN_FRAMES=2 "$tmp/gpurt" $m 64 36)" = "프레임 1개" ] &&
                   [ "$(head -c 13 "$HAN_SCREENSHOT")" = "$(printf 'P6\n64 36\n255\n')" ]; then ok
                else bad "examples/GPU/광선추적.han ($m 비트)"; sed 's/^/    /' "$tmp/err" | head -3; fi
            done
            if ./hanc examples/GPU/만델브로.han -o "$tmp/mb" 2>"$tmp/err" &&
               [ "$(HAN_FRAMES=2 "$tmp/mb")" = "프레임 2개" ]; then ok
            else bad "examples/GPU/만델브로.han"; sed 's/^/    /' "$tmp/err" | head -3; fi
        fi
    fi
else echo "  (libvulkan 이 없어서 GPU 테스트는 건너뜀)"; fi

echo "== 디버거·자동완성 (hanc --디버그 / --기호 + VS Code 확장)"
# --디버그 로 컴파일해도 결과가 같아야 함 (훅·타입 칸이 프로그램 뜻을 바꾸면 안 됨)
for f in examples/demo4.han examples/demo7.han tests/t_try.han tests/t_list.han tests/t_struct.han tests/t_holes.han tests/t_global.han tests/t_funcval.han tests/t_vec.han tests/t_dict.han tests/t_nest.han; do
    [ -f "$f" ] || continue
    exp="tests/expected/$(dirname "$f" | tr / _)_$(basename "$f" .han).out"
    if ./hanc --디버그 "$f" -o "$tmp/dbgp" 2>"$tmp/err" &&
       (cd "$tmp" && rm -f t_*.txt t_*.csv; printf '10\n20\n12\n' | HAN_SEED=1 ./dbgp 가나 12 > out 2>&1; echo "rc=$?" >> out) &&
       cmp -s "$tmp/out" "$exp"; then ok
    else bad "$f (--디버그 로 컴파일하면 결과가 다름)"; diff "$tmp/out" "$exp" 2>/dev/null | head -4 | sed 's/^/    /'; fi
done
# 어댑터(debug.js)에 VS Code 처럼 DAP 를 보내 멈춤점·한 줄씩·함수 안으로·밖으로·변수를 확인 (node 가 있을 때만)
if command -v node >/dev/null; then
    if timeout 30 node tests/debug/vscode흉내.js "$PWD/editors/vscode/debug.js" "$PWD/tests/debug/d1.han" "$PWD/hanc" 14 stepIn next stepOut next continue > "$tmp/out" 2>&1 &&
       cmp -s "$tmp/out" tests/expected/debug_d1.out; then ok
    else bad "tests/debug/d1.han (디버거 대화가 정답과 다름)"; diff "$tmp/out" tests/expected/debug_d1.out | head -6 | sed 's/^/    /'; fi
else echo "  (node 가 없어서 디버그 어댑터 시험은 건너뜀)"; fi
# 자동완성: hanc --기호 + editors/vscode/complete.js 가 자리마다 알맞은 것을 추천하나 (node 가 있을 때만)
if command -v node >/dev/null; then
    if node tests/editor/완성시험.js "$PWD/editors/vscode/complete.js" "$PWD/hanc" "$PWD/tests/editor/완성.han" > "$tmp/out" 2>&1 &&
       cmp -s "$tmp/out" tests/expected/editor_완성.out; then ok
    else bad "tests/editor/완성.han (자동완성 추천이 정답과 다름)"; diff "$tmp/out" tests/expected/editor_완성.out | head -6 | sed 's/^/    /'; fi
fi

echo "== 웹 놀이터 모드 (hanc --놀이터: 바깥 세상을 건드리는 기능은 컴파일 오류)"
for f in tests/play/p[1-2].han; do
    ./hanc --놀이터 "$f" -o "$tmp/gp" >/dev/null 2>"$tmp/err"
    if cmp -s "$tmp/err" "tests/expected/play_$(basename "$f" .han).out"; then ok
    else bad "$f (놀이터 에러 메시지가 정답과 다름)"; diff "$tmp/err" "tests/expected/play_$(basename "$f" .han).out" | head -4 | sed 's/^/    /'; fi
done
if ./hanc --놀이터 tests/play/p3.han -o "$tmp/gp" 2>"$tmp/err" &&
   [ "$(cd "$tmp" && HAN_PLAYGROUND=1 ./gp 2>&1)" = "$(printf "안에 씀\n1\n[실행 오류] 5행: 놀이터(웹)에서는 이 폴더 안의 파일만 열 수 있음 ('../밖.txt')")" ]; then ok
else bad "tests/play/p3.han (놀이터 파일 막기가 틀림)"; fi
if ./hanc --놀이터 tests/play/p5.han -o "$tmp/gp" 2>"$tmp/err" && [ "$("$tmp/gp" 2>&1)" = "$(printf '에러로 감\n3')" ]; then ok
else bad "tests/play/p5.han (놀이터에서 되는 기능이 막힘)"; fi

echo "== 셀프호스팅 (compiler/한랭.han 이 자기 자신을 컴파일)"
# 지금 hanc 로 컴파일러를 새로 만들고(1세대), 그게 자기 자신을 컴파일한 것(2세대 어셈블리)이
# ① bootstrap/hanc.s 와 같아야 하고 (다르면 컴파일러를 고친 뒤 make bootstrap 을 안 한 것)
# ② 2세대가 또 컴파일한 3세대와 같아야 한다 (고정점)
mkdir -p "$tmp/rt/runtime" && cp runtime/runtime.s runtime/runtime-window.s "$tmp/rt/runtime/"
if ./hanc compiler/한랭.han -o "$tmp/rt/hanc" 2>"$tmp/err" &&
   "$tmp/rt/hanc" compiler/한랭.han > "$tmp/g2.s" && gcc -no-pie "$tmp/g2.s" -o "$tmp/rt/hanc2" -lm -lpthread &&
   "$tmp/rt/hanc2" compiler/한랭.han > "$tmp/g3.s"; then
    if cmp -s "$tmp/g2.s" "$tmp/g3.s"; then ok; else bad "compiler/한랭.han (2세대와 3세대가 다름)"; fi
    if cmp -s "$tmp/g2.s" bootstrap/hanc.s; then ok
    else bad "bootstrap/hanc.s 가 컴파일러 소스보다 옛것 (make bootstrap 하기)"; fi
else
    bad "compiler/한랭.han (컴파일 실패)"; sed 's/^/    /' "$tmp/err" | head -5
fi
# -o 로 실행파일까지 만들고 중간 .s 는 지운다
if ./hanc tests/t_lib.han -o "$tmp/tl" 2>"$tmp/err" && [ ! -e "$tmp/tl.s" ] && "$tmp/tl" >/dev/null; then ok
else bad "hanc -o (실행파일 만들기)"; fi
# 컴파일러는 가장 큰 한랭 프로그램이라 GC 검증에도 좋다: 할당마다 수거해도 같은 결과
if ./hanc tests/t_basic2.han > "$tmp/n0.s" &&
   HAN_GC_STRESS=1 timeout 120 ./hanc tests/t_basic2.han > "$tmp/n.s" && cmp -s "$tmp/n0.s" "$tmp/n.s"; then ok
else bad "hanc (수거 스트레스 모드에서 결과가 달라짐)"; fi

echo "== 에러 메시지 (컴파일 실패가 정답)"
for f in tests/e*.han; do
    run_rejects "$f"
done

echo
if [ "$fail" -eq 0 ]; then
    echo "전부 통과 ($pass 개)"
else
    echo "$fail 개 실패 / $((pass+fail)) 개 중"
    exit 1
fi
