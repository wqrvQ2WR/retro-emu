# 앱 아이콘 생성: 그라디언트 + 흰 게임패드. python3 assets/mkicon.py
from PIL import Image, ImageDraw
S = 1024
def grad(size):
    im = Image.new("RGB", (size, size)); px = im.load()
    stops = [(0, (255, 77, 157)), (0.5, (155, 92, 255)), (1, (61, 169, 255))]
    for y in range(size):
        for x in range(size):
            t = (x + y) / (2 * (size - 1))
            for (a, ca), (b, cb) in zip(stops, stops[1:]):
                if a <= t <= b:
                    k = (t - a) / (b - a); px[x, y] = tuple(int(ca[i] + (cb[i] - ca[i]) * k) for i in range(3)); break
    return im
def pad(d, s=1.0, off=0):
    c = S // 2; o = off
    W = (255, 255, 255, 255)
    d.rounded_rectangle([c - 330*s, c - 190*s + o, c + 330*s, c + 190*s + o], radius=170*s, fill=W)
    # 십자키(그라디언트 색 구멍)
    h = (155, 92, 255, 255); x0, y0 = c - 190*s, c + o
    d.rounded_rectangle([x0 - 24*s, y0 - 80*s, x0 + 24*s, y0 + 80*s], radius=10*s, fill=h)
    d.rounded_rectangle([x0 - 80*s, y0 - 24*s, x0 + 80*s, y0 + 24*s], radius=10*s, fill=h)
    for dx, dy in [(150, 30), (230, -40)]:
        d.ellipse([c + dx*s - 42*s, c + dy*s + o - 42*s, c + dx*s + 42*s, c + dy*s + o + 42*s], fill=(255, 77, 157, 255))
# 전체 아이콘(둥근 사각형)
base = grad(S).convert("RGBA"); fg = Image.new("RGBA", (S, S), (0, 0, 0, 0)); pad(ImageDraw.Draw(fg)); base.alpha_composite(fg)
mask = Image.new("L", (S, S), 0); ImageDraw.Draw(mask).rounded_rectangle([0, 0, S - 1, S - 1], radius=230, fill=255)
full = Image.new("RGBA", (S, S), (0, 0, 0, 0)); full.paste(base, (0, 0), mask); full.save("assets/icon.png")
# 안드로이드 어댑티브: 배경(그라디언트) + 전경(패드, 안전영역 안으로 축소)
grad(432).save("assets/ic_bg.png")
f = Image.new("RGBA", (S, S), (0, 0, 0, 0)); pad(ImageDraw.Draw(f), 0.78); f.resize((432, 432), Image.LANCZOS).save("assets/ic_fg.png")
