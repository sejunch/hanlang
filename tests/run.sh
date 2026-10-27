#!/usr/bin/env bash
# 한랭 회귀 테스트 전부 돌리기.  사용:  make test   또는  ./tests/run.sh
cd "$(dirname "$0")/.." || exit 1
ROOT=$PWD

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
         tests/t_tree.han tests/t_nest.han tests/t_dict.han; do
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
         tests/t_none.han tests/t_nofile.han tests/t_dictmiss.han; do
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
         tests/t_tree.han tests/t_nest.han tests/t_dict.han; do
    if ./hanc "$f" -o "$tmp/p" 2>"$tmp/err" &&
       a=$("$tmp/p" </dev/null 2>&1) && b=$(HAN_GC_STRESS=1 timeout 60 "$tmp/p" </dev/null 2>&1) &&
       [ "$a" = "$b" ]; then ok
    else bad "$f (수거 스트레스 모드에서 결과가 달라짐)"; fi
done

echo "== 셀프호스팅 (한랭으로 짠 컴파일러 self/한랭.han)"
export HAN_SEED=1          # (N의 무작위) 를 쓰는 프로그램도 양쪽 결과가 같게
# hanc 로 컴파일러(1세대)를 만들고, 그걸로 프로그램을 컴파일해서 hanc 와 결과가 같은지 본다.
# 셀프 컴파일러가 낸 어셈블리는 런타임(hanc --runtime)과 같이 링크한다.
self_build() {   # $1 = 컴파일러, $2 = 원본, $3 = 출력 .s.  에러는 표준 에러로, 실패하면 종료코드 1
    rm -f "$3"; "$1" "$2" "$3" "$ROOT/lib" >"$tmp/selfout" 2>"$tmp/selferr"
}
self_link() { gcc -no-pie "$1" "$tmp/rt.s" -o "$2" -lm 2>"$tmp/err"; }
# 출력과 종료코드를 hanc 쪽과 비교 (입력·인자·작업 폴더도 같게)
self_same() {   # $1 = 컴파일러, $2 = 원본
    self_build "$1" "$2" "$tmp/s.s" && self_link "$tmp/s.s" "$tmp/sp" &&
    [ "$(cd "$tmp" && printf '10\n20\n12\n' | ./sp 가나 12 2>&1; echo "rc=$?")" = \
      "$(cd "$tmp" && printf '10\n20\n12\n' | "$ROOT/han" "$ROOT/$2" 가나 12 2>&1; echo "rc=$?")" ]
}
# 예제·기능 테스트 전부 (입력·인자는 양쪽에 똑같이 준다)
SELF_SAME="self/tests/s1.han self/tests/s2.han $(ls examples/*.han tests/t_*.han | tr '\n' ' ')"
if ./hanc self/한랭.han -o "$tmp/hanc2" 2>"$tmp/err" && ./hanc --runtime > "$tmp/rt.s"; then
    ok
    for f in $SELF_SAME; do
        if self_same "$tmp/hanc2" "$f"; then ok
        else bad "$f (셀프 컴파일러 결과가 hanc 와 다름)"; head -3 "$tmp/selferr" | sed 's/^/    /'; fi
    done
    # 셀프호스팅: 1세대가 자기 자신을 컴파일해 2세대, 2세대가 또 컴파일해 3세대.
    # 2세대와 3세대 어셈블리가 바이트 단위로 같아야 한다 (고정점)
    if self_build "$tmp/hanc2" self/한랭.han "$tmp/g2.s" && self_link "$tmp/g2.s" "$tmp/hanc_g2" &&
       self_build "$tmp/hanc_g2" self/한랭.han "$tmp/g3.s" && cmp -s "$tmp/g2.s" "$tmp/g3.s"; then ok
    else bad "self/한랭.han (자기 자신 컴파일: 2세대와 3세대가 다름)"; head -3 "$tmp/selferr" | sed 's/^/    /'; fi
    # 2세대 컴파일러(한랭이 컴파일한 컴파일러)도 똑같이 맞아야 한다
    for f in self/tests/s2.han tests/t_tree.han tests/t_args.han; do
        if [ -x "$tmp/hanc_g2" ] && self_same "$tmp/hanc_g2" "$f"; then ok
        else bad "$f (2세대 셀프 컴파일러 결과가 hanc 와 다름)"; fi
    done
    # 셀프 컴파일러는 지금까지 짠 한랭 프로그램 중 가장 커서 GC 검증에도 좋다
    if self_build "$tmp/hanc2" self/tests/s2.han "$tmp/n0.s" &&
       HAN_GC_STRESS=1 timeout 120 "$tmp/hanc2" self/tests/s2.han "$tmp/n.s" && cmp -s "$tmp/n0.s" "$tmp/n.s"; then ok
    else bad "self/한랭.han (수거 스트레스 모드에서 결과가 달라짐)"; fi
    # 에러 메시지(문구·캐럿·호출 문맥)가 hanc 와 글자 하나까지 같아야 한다.
    # se6 은 빼고: hanc 는 문자열이 여러 줄에 걸칠 수 있어 행 번호가 다르다 (셀프 렉서는 줄 단위)
    for f in tests/e*.han self/tests/se*.han; do
        case "$f" in */se6.han) continue;; esac
        self_build "$tmp/hanc2" "$f" "$tmp/s.s"
        grep -q "셀프 컴파일러" "$tmp/selferr" && continue        # 아직 안 되는 기능 (소수·불러오기 등)
        if [ "$(./hanc "$f" -o "$tmp/p" 2>&1)" = "$(cat "$tmp/selferr")" ]; then ok
        else bad "$f (셀프 컴파일러 에러 메시지가 hanc 와 다름)"; fi
    done
    for f in self/tests/se*.han; do                           # 컴파일 에러가 나야 하는 것들
        if self_build "$tmp/hanc2" "$f" "$tmp/s.s"; then bad "$f (셀프 컴파일러: 컴파일 에러가 나야 정상)"
        elif [ -f "$tmp/s.s" ]; then bad "$f (셀프 컴파일러: 오류인데 어셈블리를 만듦)"
        elif ! grep -q '^\[오류\]' "$tmp/selferr" || [ -s "$tmp/selfout" ]; then
            bad "$f (셀프 컴파일러: 오류 메시지는 표준 에러로만 나와야 함)"
        else ok; fi
    done
else
    bad "self/한랭.han (hanc 로 컴파일 실패)"; sed 's/^/    /' "$tmp/err" | head -5
fi

unset HAN_SEED

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
