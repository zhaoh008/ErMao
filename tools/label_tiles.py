from PIL import Image, ImageDraw, ImageFont
import os
src = r"D:\GameEngine\engine\assets\sprites\kenney_tilemap.png"
img = Image.open(src).convert("RGBA")
cols, rows = img.width // 16, img.height // 16
scale = 3
out = Image.new("RGBA", (cols*16*scale, rows*16*scale), (40,40,48,255))
draw = ImageDraw.Draw(out)
font = ImageFont.truetype(r"C:\Windows\Fonts\consola.ttf", 14)
for y in range(rows):
    for x in range(cols):
        tile = img.crop((x*16, y*16, x*16+16, y*16+16)).resize((16*scale, 16*scale), Image.NEAREST)
        out.alpha_composite(tile, (x*16*scale, y*16*scale))
        idx = y*cols + x
        draw.text((x*16*scale+2, y*16*scale+1), str(idx), fill=(255,255,0,255), font=font)
out.save(r"D:\GameEngine\docs\figures\kenney-tiles-labeled.png")
print(cols, rows, out.size)