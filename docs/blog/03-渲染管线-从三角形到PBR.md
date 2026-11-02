# P3 · 渲染管线：从三角形到 PBR

> 一帧画面是怎么从 4×4 矩阵变成一个像素的？

## 细纲

### 1. RenderScene：渲染器的输入
- 扁平数据结构的意义
- 为什么不在渲染器里遍历世界

### 2. 变换与实例化
- MVP 与齐次除法
- 144 B 实例布局、法线矩阵
- 绕序与背面剔除

### 3. PBR：Cook-Torrance
- D/G/F 三项分别管什么
- 一个像素的数值演算（含 ACES/gamma）
- 金属度/粗糙度怎么影响外观

### 4. HDR 与色调映射
- 为什么先渲到 RGBA16F
- ACES 拟合曲线
- sRGB 与线性空间的坑

### 5. 调试与踩坑
- 法线反向、sRGB 双重转换、深度精度
- 用 --roughness/--metallic 扫参验证

## 素材

- 配图：fig-material-grid.png, demo-wave.png
- 代码：shaders/PBR.hlsl, Renderer::OpaquePass, L05 数值演算
- 实验：`ErMao.exe --level arena --play --noeditor --roughness 0.2 --metallic 0.9`
- 数学参考：Cook-Torrance 1982, GGX / Karis 2013, Smith 1967, Schlick 1994, ACES

## 自检问题

1. D 和 G 各自解决什么问题？
2. 为什么金属的 F0 是 albedo？
3. ACES 之前为什么必须在线性空间？

## 状态

- [ ] 初稿
- [ ] 自测（能答 4/5）
- [ ] 冷却 48h 复读
- [ ] 读者测试
- [ ] 发布
