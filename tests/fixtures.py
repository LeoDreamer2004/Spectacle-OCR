"""Synthetic OCR samples with known ground truth; no screen content is captured."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

FONT = "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc"
LINES = ["Spectacle OCR 2026", "中文识别测试", "Hello World 12345"]


def generate(directory):
    directory = Path(directory)
    directory.mkdir(parents=True, exist_ok=True)
    images = {}
    for name, size, background, foreground in [
        ("text", 32, "white", "black"),
        ("dark", 24, "#202020", "#eeeeee"),
        ("small", 16, "white", "black"),
    ]:
        font = ImageFont.truetype(FONT, size)
        image = Image.new("RGB", (900, 240), background)
        draw = ImageDraw.Draw(image)
        for row, line in enumerate(LINES):
            draw.text((30, 20 + row * 65), line, font=font, fill=foreground)
        image.save(directory / f"{name}.png")
        image.save(directory / f"{name}.ppm")
        images[name] = image
    images["blank"] = Image.new("RGB", (640, 480), "white")
    images["blank"].save(directory / "blank.png")
    return images
