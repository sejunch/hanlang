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
for f in examples/demo4.han examples/demo7.han tests/t_list.han tests/t_str.han \
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
for f in $(ls examples/*.han tests/t_*.han | grep -v t_win); do
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

echo "== 셀프호스팅 (compiler/한랭.han 이 자기 자신을 컴파일)"
# 지금 hanc 로 컴파일러를 새로 만들고(1세대), 그게 자기 자신을 컴파일한 것(2세대 어셈블리)이
# ① bootstrap/hanc.s 와 같아야 하고 (다르면 컴파일러를 고친 뒤 make bootstrap 을 안 한 것)
# ② 2세대가 또 컴파일한 3세대와 같아야 한다 (고정점)
mkdir -p "$tmp/rt/runtime" && cp runtime/runtime.s runtime/runtime-window.s "$tmp/rt/runtime/"
if ./hanc compiler/한랭.han -o "$tmp/rt/hanc" 2>"$tmp/err" &&
   "$tmp/rt/hanc" compiler/한랭.han > "$tmp/g2.s" && gcc -no-pie "$tmp/g2.s" -o "$tmp/rt/hanc2" -lm &&
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
