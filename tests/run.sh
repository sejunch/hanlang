#!/usr/bin/env bash
# 한랭 회귀 테스트 전부 돌리기.  사용:  make test   또는  ./tests/run.sh
cd "$(dirname "$0")/.." || exit 1

pass=0; fail=0
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
         tests/t_tree.han tests/t_nest.han; do
    run_ok "$f"
done
run_ok tests/t_eof.han "$(printf '10\n20\n12\n')"
# 파일을 만드는 테스트는 임시 폴더 안에서 돌리고, 결과 파일 내용까지 본다
if ./hanc tests/t_file.han -o "$tmp/tf" 2>"$tmp/err" && (cd "$tmp" && ./tf >/dev/null) &&
   [ "$(cat "$tmp/t_file_copy.txt")" = "$(printf '> 첫째 줄\n> 42\n> 3.5 / 끝\n> 덧붙인 줄')" ]; then ok
else bad "tests/t_file.han"; sed 's/^/    /' "$tmp/err" | head -3; fi

echo "== 실행 오류 (종료코드 1 이 정답)"
for f in tests/t_div0.han tests/t_mod0.han tests/t_oob.han tests/t_gerr.han tests/t_shift.han tests/t_popempty.han \
         tests/t_none.han tests/t_nofile.han; do
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
         tests/t_tree.han tests/t_nest.han; do
    if ./hanc "$f" -o "$tmp/p" 2>"$tmp/err" &&
       a=$("$tmp/p" </dev/null 2>&1) && b=$(HAN_GC_STRESS=1 timeout 60 "$tmp/p" </dev/null 2>&1) &&
       [ "$a" = "$b" ]; then ok
    else bad "$f (수거 스트레스 모드에서 결과가 달라짐)"; fi
done

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
