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

echo "== 기능"
for f in tests/t_float.han tests/t_func.han tests/t_edge.han tests/t_list.han \
         tests/t_builtin.han tests/t_str.han tests/t_lib.han tests/t_mok.han \
         tests/t_poly.han tests/t_strcmp.han tests/t_grid.han tests/t_loop.han \
         tests/t_split.han tests/t_end.han tests/t_bit.han tests/t_code.han \
         tests/t_struct.han; do
    run_ok "$f"
done

echo "== 실행 오류 (종료코드 1 이 정답)"
for f in tests/t_div0.han tests/t_mod0.han tests/t_oob.han tests/t_gerr.han tests/t_shift.han; do
    run_dies "$f"
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
