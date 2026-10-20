"""启动 Vms.Client（带 VMS_DIAG=orgclick），等探针写完 orgclick.log / diagtrace.log 后读取并打印。

用途：三态组织复选框的"真实点击路径"取证 —— 用框架自己的 ToggleButton.OnClick 连点三次，
把每一步的控件值 / 模型值 / 联动结果落盘。避免用推理代替实测。
仅用标准库。
"""
import os
import subprocess
import sys
import time

EXE = (r"E:\win_e\cs_dev\soft_src\win_vms_v1\src\Vms.Client\bin\x64\Debug"
       r"\net10.0-windows10.0.19041.0\win-x64\Vms.Client.exe")
BASE = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Vms.Client")
LOG = os.path.join(BASE, "orgclick.log")
TRACE = os.path.join(BASE, "diagtrace.log")

TIMEOUT = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0

for p in (LOG, TRACE):
    try:
        os.remove(p)
    except FileNotFoundError:
        pass

env = dict(os.environ)
env["VMS_DIAG"] = "orgclick"

print(f"[orgclick] launching {EXE}")
proc = subprocess.Popen([EXE], env=env)

deadline = time.time() + TIMEOUT
while time.time() < deadline:
    if os.path.exists(LOG):
        break
    if proc.poll() is not None:
        print(f"[orgclick] 进程已退出 code={proc.returncode}")
        break
    time.sleep(0.5)

for title, path in (("orgclick.log", LOG), ("diagtrace.log", TRACE)):
    if os.path.exists(path):
        with open(path, encoding="utf-8", errors="replace") as f:
            content = f.read()
        print(f"========== {title} ==========")
        print(content.rstrip())
        print("=" * (len(title) + 24))
    else:
        print(f"[orgclick] 未找到 {title}（进程可能崩了，看 crash.log）")

try:
    proc.terminate()
    proc.wait(timeout=5)
except Exception as ex:
    print(f"[orgclick] 终止进程时：{ex}")
    try:
        proc.kill()
    except Exception:
        pass
