from PIL import Image

images = [
    "kunlun_tanker_cartoon_driver_winui_16_ico_ready.png",
    "kunlun_tanker_cartoon_driver_winui_32_ico_ready.png",
    "kunlun_tanker_cartoon_driver_winui_48_ico_ready.png",
    "kunlun_tanker_cartoon_driver_winui_256_ico_ready.png"
]

icon_images = [Image.open(img).convert("RGBA") for img in images]

icon_images[0].save(
    "app.ico",
    format="ICO",
    sizes=[(16, 16), (32, 32), (48, 48), (256, 256)]
)
