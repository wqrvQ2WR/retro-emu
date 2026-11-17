# 테스트용 DS 롬(저작권 없는 직접 만든 롬): 위 화면에 무늬, A 버튼을 누르면 파래진다.
# ARM9 코드는 nds/arm9.c 를 clang(ARM 대상)으로 컴파일해서 넣는다. 결과: tests/test.nds
import os, struct, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
llvm = os.environ.get("LLVM_BIN", "/opt/homebrew/opt/llvm/bin")
if not os.path.exists(os.path.join(llvm, "clang")):
    import glob
    c = sorted(glob.glob("/opt/homebrew/Cellar/llvm*/*/bin"))
    llvm = c[-1] if c else llvm
with tempfile.TemporaryDirectory() as t:
    o, b = os.path.join(t, "a.o"), os.path.join(t, "a.bin")
    subprocess.check_call([os.path.join(llvm, "clang"), "--target=armv5te-none-eabi", "-marm", "-O2", "-ffreestanding",
                           "-fno-builtin", "-nostdlib", "-c", os.path.join(here, "nds/arm9.c"), "-o", o])
    subprocess.check_call([os.path.join(llvm, "llvm-objcopy"), "-O", "binary", "--only-section=.text", o, b])
    arm9 = open(b, "rb").read()
arm7 = struct.pack("<I", 0xEAFFFFFE)  # b . (ARM7 은 그냥 돈다)

ARM9_OFF, ARM7_OFF = 0x200, 0x1000
ARM9_ADDR, ARM7_ADDR = 0x02000000, 0x02380000
rom = bytearray(0x2000)
h = bytearray(0x200)
h[0:12] = b"RETROEMUTEST"
h[12:16] = b"####"; h[16:18] = b"00"
struct.pack_into("<IIII", h, 0x20, ARM9_OFF, ARM9_ADDR, ARM9_ADDR, len(arm9))
struct.pack_into("<IIII", h, 0x30, ARM7_OFF, ARM7_ADDR, ARM7_ADDR, len(arm7))
struct.pack_into("<I", h, 0x80, len(rom))   # 쓰는 롬 크기
struct.pack_into("<I", h, 0x84, 0x200)      # 헤더 크기
def crc16(d, c=0xFFFF):
    for x in d:
        c ^= x
        for _ in range(8): c = (c >> 1) ^ 0xA001 if c & 1 else c >> 1
    return c
struct.pack_into("<H", h, 0x15E, crc16(h[:0x15E]))
rom[0:0x200] = h
rom[ARM9_OFF:ARM9_OFF + len(arm9)] = arm9
rom[ARM7_OFF:ARM7_OFF + len(arm7)] = arm7
open(os.path.join(here, "test.nds"), "wb").write(rom)
print("test.nds", len(arm9), "bytes arm9")
