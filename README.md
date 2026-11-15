# retro-emu

고전게임 에뮬레이터. 맥과 안드로이드에서 돈다. 패미컴, 게임보이/컬러/어드밴스, 슈퍼패미컴, 메가드라이브, PS1.

- `host/` 공용 C 호스트(rhost). libretro 코어를 dlopen 해서 영상(RGBA)·소리·입력·세이브를 처리
- `mac/` SwiftUI 앱. `mac/build-app.sh` -> /Applications/RetroEmu.app
- `android/` Compose + JNI(ndk-build). NDK 27.2.12479018 필요
- `fetch-cores.sh` 코어 받기(libretro 공식 nightly). 롬은 직접 넣어야 한다.
- `tests/mk_nes.py` 직접 만든 테스트 롬 생성
- `assets/mkicon.py` 앱 아이콘 생성

조작(맥): 방향키, Z/X/A/S, Q/W, Return=Start, Delete/Tab=Select, Esc=나가기, F1/F2=상태 저장/불러오기(마지막에 고른 슬롯), F3=저장 목록. 게임패드도 됨.

저장: 게임마다 슬롯 6칸 + 자동 저장 1칸. 슬롯마다 저장 순간 화면과 시각이 남는다. 5초 넘게 하고 나가면 자동 저장되고, 다음에 그 게임을 켜면 저장 목록부터 뜬다. 안드로이드는 오른쪽 위 메뉴 -> 저장 / 불러오기(길게 누르면 지우기). 파일은 saves/<롬>.<슬롯>.state/.png, 예전 <롬>.state 는 1번 슬롯으로 옮겨진다.
PS1은 BIOS 없이 HLE로 돈다(호환성 낮음). 필요하면 scph5501.bin 을 시스템 폴더에 둔다.
