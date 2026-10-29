.PHONY: all test install uninstall clean vscode bootstrap

PREFIX ?= /usr/local
CC     ?= gcc

# 한랭 컴파일러는 한랭으로 짜여 있다 (compiler/한랭.han).
# 처음 빌드할 때는 그 컴파일러가 자기 자신을 컴파일해 둔 어셈블리(bootstrap/hanc.s)를
# gcc 로 링크만 한다 — 그래서 gcc 만 있으면 된다.
# 컴파일러 소스를 고친 뒤에는 `make bootstrap` 으로 bootstrap/hanc.s 를 새로 만든다.

RTFLAGS = -O2 -S -fno-asynchronous-unwind-tables

all: hanc runtime/runtime.s runtime/runtime-window.s runtime/runtime-gpu.s

hanc: bootstrap/hanc.s
	$(CC) -no-pie bootstrap/hanc.s -o $@ -lm

# 런타임: 어셈블리 몇 줄(helpers.s) + 한랭으로 짠 것(runtime/기본.han, hanc --모듈). C 는 없다.
# 한랭 모듈 쪽은 컴파일러가 알아서 hm… / .L9억… 라벨을 써서 프로그램 라벨과 안 겹친다.
runtime/runtime.s: runtime/helpers.s runtime/기본.han hanc
	./hanc --모듈 runtime/기본.han > runtime/기본.s.tmp
	cat runtime/helpers.s runtime/기본.s.tmp > $@
	rm -f runtime/기본.s.tmp

# 창 그리기 런타임 (X11 + Xft). 창을 쓰는 프로그램에만 붙는다.
runtime/runtime-window.s: runtime/window.c
	$(CC) $(RTFLAGS) -I/usr/include/freetype2 -o - runtime/window.c | \
	  sed -e '/\.note\.GNU-stack/d' -e 's/\.L/.Lwn/g' > $@

# GPU 런타임 (Vulkan). GPU의 돌리다 를 쓰는 프로그램에만 붙는다.
# vulkan 헤더가 없으면 GPU 를 쓸 때 실행 오류만 내는 대체품(gpu_none.c)을 굽는다 — make 가 깨지지 않게
runtime/runtime-gpu.s: runtime/gpu.c runtime/gpu_none.c
	{ $(CC) $(RTFLAGS) -o runtime/gpu.s.tmp runtime/gpu.c 2>/dev/null || \
	  $(CC) $(RTFLAGS) -o runtime/gpu.s.tmp runtime/gpu_none.c; }
	sed -e '/\.note\.GNU-stack/d' -e 's/\.L/.Lgp/g' runtime/gpu.s.tmp > $@
	rm -f runtime/gpu.s.tmp

# 컴파일러 소스로 bootstrap/hanc.s 를 새로 만든다. 두 번 돌려서(새 컴파일러로 다시 컴파일)
# 결과가 같은지(고정점)까지 확인한다 — 다르면 새 컴파일러가 자기 자신을 다르게 컴파일한다는 뜻.
bootstrap: hanc runtime/runtime.s
	./hanc compiler/한랭.han -o hanc.new
	./hanc.new compiler/한랭.han > bootstrap/new.s
	$(CC) -no-pie bootstrap/new.s -o hanc.new2 -lm
	./hanc.new2 compiler/한랭.han > bootstrap/new2.s
	cmp bootstrap/new.s bootstrap/new2.s
	mv bootstrap/new.s bootstrap/hanc.s
	rm -f hanc.new hanc.new2 bootstrap/new2.s
	$(CC) -no-pie bootstrap/hanc.s -o hanc -lm
	@echo "bootstrap/hanc.s 를 새로 만듦 (고정점 확인)."

test: all
	@./tests/run.sh

# hanc 와 han 은 bin 에, 라이브러리·런타임은 lib/hanlang 에 둔다.
# hanc 는 자기 위치 기준으로 ../lib/hanlang 을 찾으므로 이대로 두면 된다.
install: all
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib/hanlang
	cp hanc han $(DESTDIR)$(PREFIX)/bin/
	chmod +x $(DESTDIR)$(PREFIX)/bin/hanc $(DESTDIR)$(PREFIX)/bin/han
	cp lib/*.han runtime/runtime.s runtime/runtime-window.s runtime/runtime-gpu.s $(DESTDIR)$(PREFIX)/lib/hanlang/
	@echo "설치 끝.  han 프로그램.han  으로 실행."

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/hanc $(DESTDIR)$(PREFIX)/bin/han
	rm -rf $(DESTDIR)$(PREFIX)/lib/hanlang

clean:
	rm -f hanc runtime/runtime.s runtime/runtime-window.s runtime/runtime-gpu.s

# VS Code 확장: .vsix 만들고 설치 (editors/vscode/)
vscode:
	python3 editors/vscode/pack.py
	code --install-extension editors/vscode/hanlang-0.1.0.vsix --force
