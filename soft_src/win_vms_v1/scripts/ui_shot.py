"""启动 Vms.Client 并在若干秒后截取主窗口，输出 PNG。

用途：UI 回归取证（树/地图/页签的渲染结果肉眼可见），避免"改了就看一眼"依赖人工。
只用标准库 + Pillow（ctypes 调 Win32 GDI）。
"""
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import time
from pathlib import Path

from PIL import Image

user32 = ctypes.windll.user32
gdi32 = ctypes.windll.gdi32

EXE = sys.argv[1] if len(sys.argv) > 1 else None
OUT = Path(sys.argv[2] if len(sys.argv) > 2 else "shot.png")
WAIT = float(sys.argv[3]) if len(sys.argv) > 3 else 14.0

pid = None
if EXE:
    proc = subprocess.Popen([EXE])
    pid = proc.pid
    print("started pid", pid)

for _ in range(int(WAIT * 10)):
    time.sleep(0.1)

EnumWindowsProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def find_window():
    found = []

    def cb(hwnd, _):
        if not user32.IsWindowVisible(hwnd):
            return True
        wpid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if pid is not None and wpid.value != pid:
            return True
        n = user32.GetWindowTextLengthW(hwnd)
        if n == 0:
            return True
        buf = ctypes.create_unicode_buffer(n + 1)
        user32.GetWindowTextW(hwnd, buf, n + 1)
        r = wt.RECT()
        user32.GetWindowRect(hwnd, ctypes.byref(r))
        if r.right - r.left < 200 or r.bottom - r.top < 200:
            return True
        found.append((hwnd, buf.value, r))
        return True

    user32.EnumWindows(EnumWindowsProc(cb), 0)
    return found


wins = find_window()
if not wins:
    print("no window found")
    sys.exit(1)
hwnd, title, r = wins[0]
print("hwnd", hwnd, repr(title), (r.left, r.top, r.right, r.bottom))

w, h = r.right - r.left, r.bottom - r.top

# PW_RENDERFULLCONTENT = 2，能带上 WebView2 这类子窗口内容
hdc = user32.GetWindowDC(hwnd)
mem = gdi32.CreateCompatibleDC(hdc)
bmp = gdi32.CreateCompatibleBitmap(hdc, w, h)
gdi32.SelectObject(mem, bmp)
ok = user32.PrintWindow(hwnd, mem, 2)
print("PrintWindow", ok)


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = [
        ("biSize", wt.DWORD), ("biWidth", ctypes.c_long), ("biHeight", ctypes.c_long),
        ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
        ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long),
        ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wt.DWORD), ("biClrImportant", wt.DWORD),
    ]


class BITMAPINFO(ctypes.Structure):
    _fields_ = [("bmiHeader", BITMAPINFOHEADER), ("bmiColors", wt.DWORD * 3)]


bi = BITMAPINFO()
bi.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
bi.bmiHeader.biWidth = w
bi.bmiHeader.biHeight = -h          # 负数 = 自上而下，省一次翻转
bi.bmiHeader.biPlanes = 1
bi.bmiHeader.biBitCount = 32
bi.bmiHeader.biCompression = 0      # BI_RGB

buf = ctypes.create_string_buffer(w * h * 4)
gdi32.GetDIBits(mem, bmp, 0, h, buf, ctypes.byref(bi), 0)

img = Image.frombuffer("RGBA", (w, h), buf, "raw", "BGRA", 0, 1).convert("RGB")
OUT.parent.mkdir(parents=True, exist_ok=True)
img.save(OUT)
print("saved", OUT, img.size)

gdi32.DeleteObject(bmp)
gdi32.DeleteDC(mem)
user32.ReleaseDC(hwnd, hdc)

# 可选：单击窗口内某点（窗口相对坐标，等于截图里的像素坐标），用于验证"点节点"这类交互
if len(sys.argv) > 4:
    rx, ry = int(sys.argv[4]), int(sys.argv[5])
    pt = wt.POINT(r.left + rx, r.top + ry)
    user32.ScreenToClient(hwnd, ctypes.byref(pt))
    lp = (pt.y << 16) | (pt.x & 0xFFFF)
    # 直接投递到该窗口：不受 z-order / 前台窗口限制（SetForegroundWindow 对非前台进程常静默失败）
    user32.PostMessageW(hwnd, 0x0200, 0, lp)          # WM_MOUSEMOVE
    user32.PostMessageW(hwnd, 0x0201, 1, lp)          # WM_LBUTTONDOWN
    user32.PostMessageW(hwnd, 0x0202, 0, lp)          # WM_LBUTTONUP
    print("clicked client", pt.x, pt.y)
    time.sleep(float(sys.argv[6]) if len(sys.argv) > 6 else 3.0)

    # 再截一张
    hdc2 = user32.GetWindowDC(hwnd)
    mem2 = gdi32.CreateCompatibleDC(hdc2)
    bmp2 = gdi32.CreateCompatibleBitmap(hdc2, w, h)
    gdi32.SelectObject(mem2, bmp2)
    user32.PrintWindow(hwnd, mem2, 2)
    buf2 = ctypes.create_string_buffer(w * h * 4)
    gdi32.GetDIBits(mem2, bmp2, 0, h, buf2, ctypes.byref(bi), 0)
    img2 = Image.frombuffer("RGBA", (w, h), buf2, "raw", "BGRA", 0, 1).convert("RGB")
    out2 = OUT.with_name(OUT.stem + "_after.png")
    img2.save(out2)
    print("saved", out2)
    gdi32.DeleteObject(bmp2)
    gdi32.DeleteDC(mem2)
    user32.ReleaseDC(hwnd, hdc2)

if EXE:
    proc.terminate()
