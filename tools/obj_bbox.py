import math
import os
for name in ["character-a", "character-skeleton", "character-zombie", "cross", "column-large"]:
    path = r"D:\GameEngine\engine\assets\models\kenney\%s.obj" % name
    xs=[]; ys=[]; zs=[]
    for line in open(path, encoding="utf-8", errors="ignore"):
        if line.startswith("v "):
            p=line.split()
            xs.append(float(p[1])); ys.append(float(p[2])); zs.append(float(p[3]))
    print(f"{name}: bbox x[{min(xs):.2f},{max(xs):.2f}] y[{min(ys):.2f},{max(ys):.2f}] z[{min(zs):.2f},{max(zs):.2f}] verts={len(xs)}")