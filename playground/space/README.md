---
title: 한랭 놀이터
emoji: 📝
colorFrom: green
colorTo: gray
sdk: docker
app_port: 7860
pinned: false
license: mit
short_description: 한글로 쓰는 교육용 프로그래밍 언어 한랭을 브라우저에서
---

# 한랭 놀이터

[한랭](https://github.com/sejunch/hanlang) 코드를 브라우저에서 바로 돌려 보는 곳.
진짜 컴파일러(hanc)로 컴파일해서 돌린다.

서버를 지키려고 `실행하다`, 낮은 수준(`요청.han` 포함)은 막고 파일은 실행마다 새로 생기는 폴더 안에서만 쓰게 했고,
한 번에 CPU 5초·메모리 512MB 까지만 돈다. 이 Space 의 파일은 저장소의 `playground/space/` 에 있다.
