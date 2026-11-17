#!/bin/bash
# 윈도우판 빌드에 쓰는 것들을 vendor/ 에 받는다(저장소에는 안 넣음).
set -e
cd "$(dirname "$0")"
mkdir -p vendor && cd vendor
curl -sSfL -o sdl2.tgz https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-devel-2.32.10-mingw.tar.gz
curl -sSfL -o ttf.tgz https://github.com/libsdl-org/SDL_ttf/releases/download/release-2.24.0/SDL2_ttf-devel-2.24.0-mingw.tar.gz
curl -sSfL -o pretendard.zip https://github.com/orioncactus/pretendard/releases/download/v1.3.9/Pretendard-1.3.9.zip
mkdir -p stb font
curl -sSfL -o stb/stb_image.h https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
curl -sSfL -o stb/stb_image_write.h https://raw.githubusercontent.com/nothings/stb/master/stb_image_write.h
tar xzf sdl2.tgz && tar xzf ttf.tgz && unzip -q -o pretendard.zip -d font
echo "준비 끝"
