#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""给每篇讲义追加「数学依据与参考资料」，并生成总参考书目。"""
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LEC = ROOT / "docs" / "lectures"

# 参考书目（键 -> 完整条目）
BIB = {
    "RTR4": "Akenine-Möller, Haines, Hoffman, Pesce, Iwanicki, Hillaire. *Real-Time Rendering*, 4th ed., CRC Press, 2018. https://www.realtimerendering.com/",
    "PBRT": "Pharr, Jakob, Humphreys. *Physically Based Rendering: From Theory to Implementation*, 3rd ed., Morgan Kaufmann, 2016. https://pbr-book.org/",
    "GAMES101": "闫令琪. *GAMES101 现代计算机图形学入门*（变换/光栅化/光线追踪/BRDF），2020. https://games-cn.org/intro-graphics/",
    "GAMES104": "王希等. *GAMES104 现代游戏引擎：从入门到实践*（Booming Tech / Piccolo Engine）. https://games104.boomingtech.com/",
    "SH": "Ramamoorthi, Hanrahan. *An Efficient Representation for Irradiance Environment Maps*. SIGGRAPH 2001.",
    "COOK": "Cook, Torrance. *A Reflectance Model for Computer Graphics*. SIGGRAPH 1982 / TOG 1982.",
    "SMITH": "Smith. *Geometrical Shadowing of a Random Rough Surface*. IEEE Trans. Antennas and Propagation, 1967.",
    "SCHLICK": "Schlick. *An Inexpensive BRDF Model for Physically-based Rendering*. Computer Graphics Forum, 1994.",
    "KARIS": "Karis. *Real Shading in Unreal Engine 4*. SIGGRAPH 2013 course notes.",
    "ACES": "Narkowicz. *ACES Filmic Tone Mapping Curve*. 2016. https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/",
    "SHADOW": "Williams. *Casting Curved Shadows on Curved Surfaces*. SIGGRAPH 1978.",
    "PCF": "Reeves, Salesin, Cook. *Rendering Antialiased Shadows with Depth Maps*. SIGGRAPH 1987.",
    "LBS": "Magnenat-Thalmann, Laperrière, Thalmann. *Joint-dependent Local Deformations for Hand Animation and Object Grasping*. Graphics Interface 1988.",
    "QUAT": "Shoemake. *Animating Rotation with Quaternion Curves*. SIGGRAPH 1985.",
    "SLERP": "Shoemake. *Animating Rotation with Quaternion Curves*. SIGGRAPH 1985.",
    "SEMI": "Semi-implicit Euler / symplectic Euler：Hairer, Lubich, Wanner. *Geometric Numerical Integration*, Springer, 2006.",
    "SAT": "Gottschalk, Lin, Manocha. *OBBTree: A Hierarchical Structure for Rapid Interference Detection*. SIGGRAPH 1996.",
    "GJK": "Gilbert, Johnson, Keerthi. *A Fast Procedure for Computing the Distance Between Complex Objects in Three-Dimensional Space*. IEEE J. Robotics and Automation, 1988.",
    "PART": "Reeves. *Particle Systems—A Technique for Modeling a Class of Fuzzy Objects*. SIGGRAPH 1983.",
    "IMGUI": "Cornut. *Dear ImGui: Bloat-free Immediate Mode Graphical User Interfaces for C++*. https://github.com/ocornut/imgui",
    "REFLECT": "Campbell. *Reflection and Serialization in Game Engines*（课程笔记/工程实践）. 另见 `docs/lectures/L14`.",
    "JSON": "ECMA-404. *The JSON Data Interchange Format*, 2013. https://www.json.org/",
    "RTREE": "Guttman. *R-Trees: A Dynamic Index Structure for Spatial Searching*. SIGMOD 1984.",
    "JOB": "Blumofe, Leiserson. *Scheduling Multithreaded Computations by Work Stealing*. JACM, 1999.",
    "AMDAHL": "Amdahl. *Validity of the Single Processor Approach to Achieving Large Scale Computing Capabilities*. AFIPS 1967.",
    "FIXED": "Fiedler. *Fix Your Timestep!* 2004. https://gafferongames.com/post/fix_your_timestep/",
    "LAG": "Bernier. *Latency Compensating Methods in Client/Server In-game Protocol Design*. GDC 2001.",
    "SNAP": "Claypool, Claypool. *Latency and Player Actions in Online Games*. CACM 2005.",
    "CULL": "Frustum culling：Akenine-Möller et al., RTR4 §19.2；GPU Driven：Wihlidal. *Optimizing the Graphics Pipeline with Compute*, 2016.",
    "SPRITE": "2D sprite batching：RTR4 §18.4；正交投影：RTR4 §2.6.",
    "OBJ": "Wavefront OBJ 格式说明（非官方规范）。https://paulbourke.net/dataformats/obj/",
    "SHADOWVOL": "Crow. *Shadow Algorithms for Computer Graphics*. SIGGRAPH 1977.",
    "TONE": "Reinhard et al. *Photographic Tone Reproduction for Digital Images*. SIGGRAPH 2002.",
    "SKIN": "Kavan et al. *Skinning with Dual Quaternions*. I3D 2007（LBS 的改进方向）。",
    "AUDIO": "PCM 与采样定理：Oppenheim, Schafer. *Discrete-Time Signal Processing*, 3rd ed., Pearson, 2010. 混音：Pirkle. *Designing Audio Effect Plugins in C++*, 2019.",
    "VK": "Khronos. *Vulkan 1.3 Specification*. https://registry.khronos.org/vulkan/",
}

# 每讲：[(原理, 数学形式, 参考键)]
REFS = {
    "L00": [
        ("齐次坐标与 4×4 变换", "p_clip = P·V·M·p_local，w 除法", ["RTR4", "GAMES101"]),
        ("背面剔除的绕序判定", "有符号面积 / 叉积符号", ["RTR4"]),
        ("三角形光栅化", "边函数（edge function）重心坐标", ["RTR4", "GAMES101"]),
    ],
    "L01": [
        ("固定步长积分与累加器", "accumulator += dt；while (acc >= h) { step(h); }", ["FIXED"]),
        ("帧率与时间复杂度", "T_frame = T_update + T_render；Amdahl 定律", ["AMDAHL"]),
        ("任务并行", "work-stealing 调度的期望时间界", ["JOB"]),
    ],
    "L02": [
        ("依赖方向与 DAG", "层间偏序：core ≺ rhi ≺ renderer ≺ scene ≺ gameplay", ["GAMES104"]),
        ("编译期解耦", "前向声明 + 指针，避免头文件循环", ["GAMES104"]),
    ],
    "L03": [
        ("TRS 矩阵与复合顺序", "M = T·R·S，列向量约定下从右往左作用", ["RTR4", "GAMES101"]),
        ("四元数与旋转", "q = cos(θ/2) + sin(θ/2)·n，单位四元数共轭即逆", ["QUAT", "RTR4"]),
        ("父子变换", "M_world = M_parent · M_local", ["GAMES104"]),
    ],
    "L04": [
        ("透视投影", "x_ndc = (x·f/aspect)/z，y_ndc = (y·f)/z", ["RTR4", "GAMES101"]),
        ("视锥剔除", "6 个平面；p-vertex 测试 dot(plane, p) > 0", ["RTR4", "CULL"]),
        ("实例化绘制的数据布局", "每实例 144 B：world(64) + 法线矩阵(48) + color(16) + 参数(16)", ["RTR4"]),
    ],
    "L05": [
        ("Cook-Torrance BRDF", "f = kd·c/π + D·G·F / (4·(N·L)(N·V))", ["COOK", "PBRT"]),
        ("GGX 法线分布", "D = α² / (π·((N·H)²(α²−1)+1)²)", ["KARIS", "PBRT"]),
        ("Smith 几何项 + Schlick 菲涅尔", "G_smith；F = F0 + (1−F0)(1−V·H)^5", ["SMITH", "SCHLICK"]),
        ("色调映射与 gamma", "ACES 拟合曲线；c^(1/2.2)", ["ACES", "TONE"]),
    ],
    "L06": [
        ("阴影贴图", "把光源当相机渲染深度；比较 d_light 与 z_light", ["SHADOW"]),
        ("PCF 软阴影", "对 3×3 邻域做深度比较后取平均", ["PCF"]),
        ("深度偏移与自阴影", "bias ∝ tan(θ)·texelWorldSize", ["RTR4"]),
    ],
    "L07": [
        ("HDR 与色调映射", "线性 HDR → 曲线压缩 → gamma", ["TONE", "ACES"]),
        ("Bloom 的卷积", "高斯核可分离：G(x,y)=G(x)·G(y)", ["RTR4"]),
    ],
    "L08": [
        ("蒙皮矩阵", "S_i = P_i · B_i⁻¹；p' = Σ w_i · S_i · p", ["LBS", "RTR4"]),
        ("权重归一化", "Σ w_i = 1，避免缩放漂移", ["LBS"]),
        ("法线变换", "N' = normalize(Σ w_i · (P_i·B_i⁻¹) 的逆转置 · N)", ["RTR4"]),
        ("LBS 缺陷", "candy-wrapper / 体积丢失 → 对偶四元数", ["SKIN"]),
    ],
    "L09": [
        ("关键帧插值", "线性插值 p(t)=lerp(p0,p1,t)", ["RTR4"]),
        ("四元数球面插值", "slerp(q0,q1,t) = (sin((1−t)Ω)q0 + sin(tΩ)q1)/sinΩ", ["SLERP"]),
        ("Cross-fade 权重", "w_A + w_B = 1，归一化混合", ["GAMES104"]),
    ],
    "L10": [
        ("半隐式欧拉", "v += a·dt；x += v·dt（辛积分，稳定）", ["SEMI"]),
        ("固定步长与穿透", "h 越小穿透越少；子步上限 kMaxSubSteps", ["FIXED"]),
        ("弹簧-阻尼", "F = −k·x − c·v", ["GAMES104"]),
    ],
    "L11": [
        ("AABB 相交测试", "区间重叠：max(a.min,b.min) ≤ min(a.max,b.max)", ["RTR4"]),
        ("SAT 分离轴", "若存在轴使投影不重叠则不相交", ["SAT"]),
        ("球-球 / 胶囊距离", "d = |c1−c2| − r1 − r2", ["GJK"]),
    ],
    "L12": [
        ("粒子系统", "发射率 λ、寿命 τ、状态积分", ["PART"]),
        ("Billboard 展开", "p = c ± right·s ± up·s", ["RTR4"]),
        ("加法混合", "dst = dst + src·α（黑底素材）", ["RTR4"]),
        ("音频混音", "s(t)=Σ g_i·x_i(t)；dB = 20·log10(g)", ["AUDIO"]),
    ],
    "L13": [
        ("射线拾取", "射线-三角形求交（Möller–Trumbore）", ["RTR4", "GAMES101"]),
        ("Gizmo 拖拽", "屏幕位移投影到世界轴：Δw = (Δs·axis_screen)/|axis_screen|²", ["GAMES101"]),
        ("Immediate Mode UI", "每帧重建 UI 顶点缓冲", ["IMGUI"]),
    ],
    "L14": [
        ("反射与类型描述", "TypeInfo{ fields[], factory } 的静态注册", ["REFLECT"]),
        ("JSON 数值往返", "最短往返浮点表示（to_chars）", ["JSON"]),
        ("确定性序列化", "字段顺序固定 + 规范化（四元数归一化阈值）", ["REFLECT"]),
    ],
    "L15": [
        ("资源缓存与引用", "key → asset；引用计数归零释放", ["GAMES104"]),
        ("OBJ 顶点去重", "哈希 (v, vt, vn) 三元组", ["OBJ"]),
        ("sRGB 与线性空间", "采样时 sRGB→linear，输出时 linear→sRGB", ["RTR4"]),
    ],
    "L16": [
        ("工作窃取调度", "期望执行时间 O(T1/P + T∞)", ["JOB"]),
        ("并行化的上限", "S(P) = 1/((1−f) + f/P)", ["AMDAHL"]),
        ("数据竞争与原子操作", "InterlockedAdd / memory order", ["JOB"]),
    ],
    "L17": [
        ("帧预算", "16.67 ms @ 60 FPS；CPU/GPU 分离计时", ["RTR4"]),
        ("GPU 时间戳", "两个 timestamp query 的差 × period", ["RTR4"]),
        ("批处理收益", "draw call 数 vs 每 draw 固定开销", ["RTR4"]),
    ],
    "L18": [
        ("客户端-服务器模型", "权威状态在服务器，客户端预测", ["LAG"]),
        ("延迟与带宽", "RTT 对射击判定的影响", ["SNAP"]),
        ("可靠 UDP", "序列号 + ACK + 重传", ["GAMES104"]),
    ],
    "L19": [
        ("状态同步", "快照 + 增量（delta）编码", ["GAMES104"]),
        ("插值与外推", "render time = now − interpolationDelay", ["LAG"]),
        ("回滚与重放", "输入缓冲 + 确定性重演", ["LAG"]),
    ],
    "L20": [
        ("构建管线", "CMake 配置 → 编译 → 链接 → 复制资源", ["GAMES104"]),
        ("包体积与资源", "assets 去重；纹理压缩", ["GAMES104"]),
    ],
    "L21": [
        ("球谐函数", "9 个 SH 系数表示低频环境光", ["SH"]),
        ("辐照度卷积", "E(n) = Σ l_m Y_m(n)", ["SH", "PBRT"]),
        ("蒙特卡洛积分", "E ≈ (1/N)Σ f(x)/pdf(x)", ["PBRT"]),
    ],
    "L22": [
        ("视锥剔除", "6 平面 p-vertex 测试", ["RTR4", "CULL"]),
        ("GPU 间接绘制", "DrawIndexedInstancedIndirect + 参数缓冲", ["CULL"]),
        ("并行压缩", "InterlockedAdd 追加到压缩列表", ["CULL"]),
    ],
    "L23": [
        ("正交投影", "x_ndc = 2(x−l)/(r−l) − 1", ["RTR4", "GAMES101"]),
        ("精灵批处理", "按纹理分组，减少状态切换", ["SPRITE"]),
        ("序列帧 UV", "u ∈ [i/N, (i+1)/N]", ["RTR4"]),
    ],
    "L25": [
        ("采样定理", "fs > 2·f_max；x[n] = x(n/fs)", ["AUDIO"]),
        ("PCM 量化", "q[n] = round(x[n]·32767)，SNR ≈ 6.02·B dB", ["AUDIO"]),
        ("混音", "y[n] = Σ g_i·x_i[n]；dB = 20·log10(g)", ["AUDIO"]),
        ("重采样/变调", "线性插值 x(t) ≈ (1−α)x[n] + αx[n+1]", ["AUDIO"]),
    ],
    "L26": [
        ("交换链与呈现", "surface caps / present mode / extent", ["VK"]),
        ("显式同步", "semaphore + fence，barrier 管理 layout", ["VK"]),
        ("渲染通道", "loadOp/storeOp/layout 转换", ["VK"]),
        ("SPIR-V", "dxc -spirv 生成的中间表示", ["VK"]),
    ],
    "L24": [
        ("OBJ 索引去重", "哈希 (v, vt, vn)", ["OBJ"]),
        ("相机朝向反解", "yaw = atan2(−d.x, −d.z)，pitch = asin(d.y)", ["RTR4"]),
        ("带贴图粒子的加法混合", "dst += src·α", ["PART"]),
        ("透视投影与深度", "z_ndc = (f+n)/(f−n) − 2fn/((f−n)z)", ["RTR4"]),
    ],
}


def section_for(key: str) -> str:
    rows = REFS.get(key, [])
    if not rows:
        return ""
    lines = ["", "## 数学依据与参考资料", "",
             "> 目的：不要求背下推导，但要知道这一讲**用的是什么数学工具**、去哪里查。",
             "", "| 用到的原理 | 数学形式 / 名称 | 参考 |", "|---|---|---|"]
    for principle, form, keys in rows:
        ref = "；".join(f"[{k}]" for k in keys if k in BIB)
        lines.append(f"| {principle} | {form} | {ref} |")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    count = 0
    for path in sorted(LEC.glob("L*.md")):
        key = path.stem.split("-")[0]
        text = path.read_text(encoding="utf-8")
        if "## 数学依据与参考资料" in text:
            continue
        text = text.rstrip() + "\n" + section_for(key)
        path.write_text(text, encoding="utf-8")
        count += 1

    # 总参考书目
    bib_lines = ["# 参考资料总表", "",
                 "> 讲义里用 `[键]` 引用，这里是完整条目。数学部分以 RTR4 / PBRT / GAMES101 为主，",
                 "> 工程实现部分以 GAMES104 与各论文/规范为准。", ""]
    for key, entry in BIB.items():
        bib_lines.append(f"- **[{key}]** {entry}")
    (LEC / "参考资料.md").write_text("\n".join(bib_lines) + "\n", encoding="utf-8")
    print(f"更新 {count} 篇讲义；参考书目 {len(BIB)} 条")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
