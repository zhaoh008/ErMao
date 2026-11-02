import struct
from pathlib import Path
out = ["// 由 tools/gen_vk_spirv.py 生成：dxc -spirv 的三角形着色器字节码", "#pragma once", "#include <cstdint>", ""]
for name, path in [("kTriangleVertSpirv", "engine/shaders/vk/triangle.vert.spv"),
                   ("kTriangleFragSpirv", "engine/shaders/vk/triangle.frag.spv")]:
    data = Path(path).read_bytes()
    words = struct.unpack("<%dI" % (len(data)//4), data)
    out.append(f"static const uint32_t {name}[] = {{")
    line = "    "
    for i, w in enumerate(words):
        line += f"0x{w:08x}, "
        if (i + 1) % 8 == 0:
            out.append(line.rstrip())
            line = "    "
    if line.strip():
        out.append(line.rstrip())
    out.append("};")
    out.append("")
Path("engine/src/rhi/VulkanTriangleSpirv.h").write_text("\n".join(out), encoding="utf-8")
print("generated", len(out), "lines")
