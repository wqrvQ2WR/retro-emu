# retro-emu

고전게임 에뮬레이터. 맥과 안드로이드에서 돈다. 패미컴, 게임보이/컬러/어드밴스, 슈퍼패미컴, 메가드라이브, PS1.

- `host/` 공용 C 호스트(rhost). libretro 코어를 dlopen 해서 영상(RGBA)·소리·입력·세이브를 처리
- `mac/` SwiftUI 앱. `mac/build-app.sh` -> ~/Applications/RetroEmu.app
- `android/` Compose + JNI(ndk-build). NDK 27.2.12479018 필요
- `fetch-cores.sh` 코어 받기(libretro 공식 nightly). 롬은 직접 넣어야 한다.
- `tests/mk_nes.py` 직접 만든 테스트 롬 생성
- `assets/mkicon.py` 앱 아이콘 생성

조작(맥): 방향키, Z/X/A/S, Q/W, Return=Start, Delete/Tab=Select, Esc=나가기, F1/F2=상태 저장/불러오기. 게임패드도 됨.
PS1은 BIOS 없이 HLE로 돈다(호환성 낮음). 필요하면 scph5501.bin 을 시스템 폴더에 둔다.
