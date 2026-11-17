# 테스트용 PSP 실행 파일(저작권 없는 직접 만든 것): tests/test-psp.elf
# psp/main.c 를 clang(mipsel)로 컴파일하고 .text 를 떼서 ELF 를 손으로 만든다(링커 없이).
import glob, os, struct, subprocess, tempfile
here = os.path.dirname(os.path.abspath(__file__))
llvm = sorted(glob.glob("/opt/homebrew/Cellar/llvm*/*/bin"))[-1]
BASE = 0x08804000
STUB0 = BASE + 0x400            # 함수 자리(코드는 0x400 바이트 안에 들어간다)
with tempfile.TemporaryDirectory() as t:
    o, b = os.path.join(t, "a.o"), os.path.join(t, "a.bin")
    subprocess.check_call([os.path.join(llvm, "clang"), "--target=mipsel-none-elf", "-march=mips2", "-mabi=32", "-O2",
                           "-ffreestanding", "-fno-builtin", "-nostdlib", "-mno-abicalls", "-fno-pic", "-G0",
                           "-DSTUB0=0x%x" % STUB0, "-c", os.path.join(here, "psp/main.c"), "-o", o])
    subprocess.check_call([os.path.join(llvm, "llvm-objcopy"), "-O", "binary", "--only-section=.text.start", o, b])
    code = bytearray(open(b, "rb").read())
    # 링커가 없으니 j 명령(R_MIPS_26, 절대 주소)만 직접 고친다: 섹션 안 위치 + 올릴 주소
    rel = subprocess.check_output([os.path.join(llvm, "llvm-objdump"), "-r", o], text=True)
    for line in rel.splitlines():
        f = line.split()
        if len(f) >= 3 and f[1] == "R_MIPS_26":
            off = int(f[0], 16)
            ins = struct.unpack_from("<I", code, off)[0]
            tgt = ((ins & 0x3FFFFFF) << 2) + BASE
            struct.pack_into("<I", code, off, (ins & 0xFC000000) | ((tgt >> 2) & 0x3FFFFFF))
    code = bytes(code)

BASE = 0x08804000
assert len(code) <= 0x400, len(code)
code += b"\0" * (0x400 - len(code))
# 가져오기: sceDisplaySetFrameBuf, sceDisplayWaitVblankStart. 자리마다 jr ra; nop (PPSSPP 가 syscall 로 바꿈)
stubs = struct.pack("<IIII", 0x03E00008, 0, 0x03E00008, 0)
nids_addr = STUB0 + len(stubs)
nids = struct.pack("<II", 0x289D82FE, 0x984C27E7)
libname_addr = nids_addr + len(nids)
libname = b"sceDisplay\0\0"
entry_addr = libname_addr + len(libname)
entry = struct.pack("<IHHBBHII", libname_addr, 0x4001, 0x0011, 5, 0, 2, nids_addr, STUB0)
modinfo_addr = entry_addr + len(entry)
name = b"retroemu-test"
modinfo = struct.pack("<HBB28sIIIII", 0, 1, 1, name.ljust(28, b"\0"), 0, modinfo_addr, modinfo_addr, entry_addr, entry_addr + len(entry))
body = code + stubs + nids + libname + entry + modinfo
code = code + stubs + nids + libname + entry   # .text 크기에 포함
shstr = b"\0.text\0.rodata.sceModuleInfo\0.shstrtab\0"
EH, PH, SH = 52, 32, 40
data_off = EH + PH
shstr_off = data_off + len(body)
sh_off = (shstr_off + len(shstr) + 3) & ~3
ehdr = struct.pack("<16sHHIIIIIHHHHHH", b"\x7fELF\x01\x01\x01" + b"\0" * 9, 2, 8, 1, BASE, EH, sh_off,
                   0x10A23001, EH, PH, 1, SH, 4, 3)
phdr = struct.pack("<IIIIIIII", 1, data_off, BASE, modinfo_addr, len(body), len(body), 7, 16)
def sh(name, typ, flags, addr, off, size, align): return struct.pack("<IIIIIIIIII", name, typ, flags, addr, off, size, 0, 0, align, 0)
shdrs = sh(0, 0, 0, 0, 0, 0, 0)
shdrs += sh(1, 1, 6, BASE, data_off, len(code), 16)
shdrs += sh(7, 1, 2, modinfo_addr, data_off + len(code), len(modinfo), 4)
shdrs += sh(29, 3, 0, 0, shstr_off, len(shstr), 1)
out = ehdr + phdr + body + shstr
out += b"\0" * (sh_off - len(out)) + shdrs
open(os.path.join(here, "test-psp.elf"), "wb").write(out)
print("test-psp.elf", len(out))

# 같은 ELF 를 EBOOT.PBP 로도 싼다(앱 목록은 .pbp 를 PSP 로 본다). PARAM.SFO 등은 비워 둔다.
elf = out
hdr_len = 0x28
offs = [hdr_len] * 6 + [hdr_len, hdr_len + len(elf)]   # sfo, icon0, icon1, pic0, pic1, snd0, data.psp, data.psar
pbp = b"\0PBP" + struct.pack("<I", 0x10000) + struct.pack("<8I", *offs) + elf
open(os.path.join(here, "test-psp.pbp"), "wb").write(pbp)
print("test-psp.pbp", len(pbp))
