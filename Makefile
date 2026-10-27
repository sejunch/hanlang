.POSIX:
.PHONY: all test install uninstall clean vscode

PREFIX ?= /usr/local
CXX    ?= g++
CXXFLAGS ?= -std=c++17 -O2

all: hanc

hanc: hanc.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

test: hanc
	@./tests/run.sh

# hanc 와 han 은 bin 에, 라이브러리는 lib/hanlang 에 둔다.
# hanc 는 자기 위치 기준으로 ../lib/hanlang 을 찾으므로 이대로 두면 된다.
install: hanc
	mkdir -p $(DESTDIR)$(PREFIX)/bin $(DESTDIR)$(PREFIX)/lib/hanlang
	cp hanc han $(DESTDIR)$(PREFIX)/bin/
	chmod +x $(DESTDIR)$(PREFIX)/bin/hanc $(DESTDIR)$(PREFIX)/bin/han
	cp lib/*.han $(DESTDIR)$(PREFIX)/lib/hanlang/
	@echo "설치 끝.  han 프로그램.han  으로 실행."

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/hanc $(DESTDIR)$(PREFIX)/bin/han
	rm -rf $(DESTDIR)$(PREFIX)/lib/hanlang

clean:
	rm -f hanc *.s

# VS Code 확장: .vsix 만들고 설치 (editors/vscode/)
vscode:
	python3 editors/vscode/pack.py
	code --install-extension editors/vscode/hanlang-0.1.0.vsix --force
