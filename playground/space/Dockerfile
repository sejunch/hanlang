# 한랭 놀이터 — Hugging Face Spaces (Docker) 용.
# 빌드할 때 GitHub 의 한랭 저장소(main)를 받아 컴파일러를 만들고 playground/server.py 를 띄운다.
# 저장소를 고친 뒤 놀이터에 반영하려면 Space 설정에서 "Factory rebuild".
FROM debian:bookworm-slim

# gcc: 한랭 컴파일러가 낸 어셈블리를 링크 / X11·Xft·Xvfb·한글 글꼴: 창 그리기 그림 / python3: 서버
RUN apt-get update && apt-get install -y --no-install-recommends \
        gcc libc6-dev make python3 git ca-certificates \
        xvfb libx11-dev libxft-dev fonts-noto-cjk \
    && rm -rf /var/lib/apt/lists/*

# Spaces 는 uid 1000 으로 돌린다
RUN useradd -m -u 1000 user
USER user
WORKDIR /home/user

ARG HANLANG_REF=main
RUN git clone --depth 1 --branch ${HANLANG_REF} https://github.com/sejunch/hanlang.git
WORKDIR /home/user/hanlang
RUN make hanc runtime/runtime.s runtime/runtime-window.s

ENV PORT=7860
EXPOSE 7860
CMD ["python3", "playground/server.py"]
