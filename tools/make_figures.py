# 把教学截图拼成对比图（带标签），输出到 docs/figures/
from PIL import Image, ImageDraw, ImageFont
from pathlib import Path

ROOT = Path(r"D:\GameEngine\docs\figures")
FONT_PATH = r"C:\Windows\Fonts\msyh.ttc"

def load(path):
    return Image.open(path).convert("RGB")

def crop_center(img, box=None):
    w, h = img.size
    if box is None:
        box = (int(w * 0.18), int(h * 0.05), int(w * 0.82), int(h * 0.95))
    return img.crop(box)

def label(img, text, scale=1.0):
    draw = ImageDraw.Draw(img)
    try:
        font = ImageFont.truetype(FONT_PATH, int(22 * scale))
    except Exception:
        font = ImageFont.load_default()
    pad = 8
    bbox = draw.textbbox((0, 0), text, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    draw.rectangle([0, 0, tw + pad * 2, th + pad * 2], fill=(15, 17, 22))
    draw.text((pad, pad - 2), text, fill=(235, 238, 245), font=font)
    return img

def grid(paths, labels, cols, out_name, tile_w=620):
    tiles = []
    for p, lab in zip(paths, labels):
        img = crop_center(load(p))
        ratio = tile_w / img.width
        img = img.resize((tile_w, int(img.height * ratio)), Image.LANCZOS)
        label(img, lab)
        tiles.append(img)
    rows = (len(tiles) + cols - 1) // cols
    th = max(t.height for t in tiles)
    canvas = Image.new("RGB", (cols * tile_w, rows * th), (10, 11, 14))
    for i, t in enumerate(tiles):
        canvas.paste(t, ((i % cols) * tile_w, (i // cols) * th))
    canvas.save(ROOT / out_name, quality=92)
    print(out_name, canvas.size)

grid([ROOT / "shadow-on.png", ROOT / "shadow-off.png", ROOT / "debug-shadow.png"],
     ["阴影开（Shadow Map + 3x3 PCF）", "阴影关（shadowParams.w = 0）", "调试视图：阴影深度（F3 -> 1）"],
     2, "fig-shadow-compare.png")

grid([ROOT / "mat-r01-m0.png", ROOT / "mat-r09-m0.png", ROOT / "mat-r01-m1.png", ROOT / "mat-r09-m1.png"],
     ["roughness=0.1  metallic=0", "roughness=0.9  metallic=0",
      "roughness=0.1  metallic=1", "roughness=0.9  metallic=1"],
     2, "fig-material-grid.png")
