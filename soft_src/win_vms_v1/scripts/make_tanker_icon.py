"""从 icon/3 的卡通罐车 PNG 生成带透明背景的图标，供 XAML 树节点与地图 marker 使用。

源 PNG 是 1536x1536 RGB、白底（无 alpha）。白底直接用在浅色界面/地图上会出现难看的白方块，
所以这里按亮度把"接近白的背景"抠成透明，并按 64x64 输出（树里缩到 16、地图里缩到 ~22 都够清晰）。

用法：python scripts/make_tanker_icon.py
依赖：Pillow（.nuget/venv 里已装）
"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "icon" / "3" / "kunlun_tanker_cartoon_driver_winui_256_ico_ready.png"
OUT = ROOT / "src" / "Vms.Client" / "WebAssets" / "vms_tanker.png"
SIZE = 64


def main() -> None:
    from PIL import Image

    if not SRC.exists():
        raise SystemExit(f"源文件不存在：{SRC}")

    im = Image.open(SRC).convert("RGBA")
    im = im.resize((SIZE, SIZE), Image.LANCZOS)

    px = im.load()
    for y in range(SIZE):
        for x in range(SIZE):
            r, g, b, a = px[x, y]
            lum = (r + g + b) / 3.0
            if lum > 250:                      # 纯白背景 → 完全透明
                px[x, y] = (r, g, b, 0)
            elif lum > 235:                    # 抗锯齿过渡带 → 柔和淡出，消除白边光晕
                alpha = int((250 - lum) / (250 - 235) * 255)
                px[x, y] = (r, g, b, alpha)
            # 其余（彩色主体）保持不透明

    OUT.parent.mkdir(parents=True, exist_ok=True)
    im.save(OUT)
    print(f"已生成：{OUT}  ({SIZE}x{SIZE}, RGBA，白底已转透明)")


if __name__ == "__main__":
    main()
