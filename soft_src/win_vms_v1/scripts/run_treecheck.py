"""启动 Vms.Client（带 VMS_DIAG=treecheck），等校验写完 treecheck.log 后读取并打印，最后结束进程。
仅用标准库。"""
import os
import subprocess
import sys
import time

EXE = (r"E:\win_e\cs_dev\soft_src\win_vms_v1\src\Vms.Client\bin\x64\Debug"
       r"\net10.0-windows10.0.19041.0\win-x64\Vms.Client.exe")
LOG = os.path.join(os.environ.get("LOCALAPPDATA", ""), "Vms.Client", "treecheck.log")

TIMEOUT = float(sys.argv[1]) if len(sys.argv) > 1 else 25.0

# 清掉上一次的旧结果，避免读到历史 PASS/FAIL
try:
    os.remove(LOG)
except FileNotFoundError:
    pass

env = dict(os.environ)
env["VMS_DIAG"] = "treecheck"

print(f"[treecheck] launching {EXE}")
proc = subprocess.Popen([EXE], env=env)

deadline = time.time() + TIMEOUT
result = None
while time.time() < deadline:
    if os.path.exists(LOG):
        try:
            with open(LOG, encoding="utf-8") as f:
                content = f.read()
            if "PASS" in content or "FAIL" in content:
                result = content
                break
        except OSError:
            pass
    if proc.poll() is not None:
        print(f"[treecheck] 进程已退出 code={proc.returncode}")
        break
    time.sleep(0.5)

if result is None:
    print("[treecheck] 超时：未检测到校验结果（进程可能崩了，看 crash.log）")
else:
    print("========== treecheck.log ==========")
    print(result.rstrip())
    print("===================================")

# 收尾：结束进程（UI 回归取证，不需要它常驻）
try:
    proc.terminate()
    proc.wait(timeout=5)
except Exception as ex:
    print(f"[treecheck] 终止进程时：{ex}")
    try:
        proc.kill()
    except Exception:
        pass
