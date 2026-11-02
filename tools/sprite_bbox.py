from PIL import Image
import os
sheets = {
 "soldier_idle": r"D:\GameEngine\engine\assets\sprites\soldier_idle.png",
 "soldier_walk": r"D:\GameEngine\engine\assets\sprites\soldier_walk.png",
 "soldier_attack": r"D:\GameEngine\engine\assets\sprites\soldier_attack.png",
 "orc_idle": r"D:\GameEngine\engine\assets\sprites\orc_idle.png",
 "orc_walk": r"D:\GameEngine\engine\assets\sprites\orc_walk.png",
 "orc_death": r"D:\GameEngine\engine\assets\sprites\orc_death.png",
 "arrow": r"D:\GameEngine\engine\assets\sprites\arrow.png",
}
for name, path in sheets.items():
    img = Image.open(path).convert("RGBA")
    w, h = img.size
    cols = max(1, w // h)
    fw = w // cols
    x0, y0, x1, y1 = fw, h, 0, 0
    for c in range(cols):
        frame = img.crop((c*fw, 0, (c+1)*fw, h))
        bbox = frame.getbbox()
        if not bbox: continue
        x0 = min(x0, bbox[0]); y0 = min(y0, bbox[1])
        x1 = max(x1, bbox[2]); y1 = max(y1, bbox[3])
    print(f"{name}: sheet={w}x{h} frame={fw}x{h} cols={cols} bbox=({x0},{y0},{x1},{y1}) "
          f"uv=({x0/fw:.3f},{y0/h:.3f},{x1/fw:.3f},{y1/h:.3f}) size={x1-x0}x{y1-y0}")