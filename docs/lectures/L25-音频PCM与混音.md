# 第 25 讲：音频——PCM、混音与 XAudio2【进阶·已实现】

> 游戏不能只有画面。这一讲把「声音」的最小闭环打通：读 WAV → 交给 XAudio2 → 混音输出。
> 代码：`platform/Audio.h/.cpp`；素材：`assets/audio/*.wav`（CC0，见 `assets-credits.md`）。
> 运行：`--audio-test`（依次播放 click/shoot/hit/pickup/levelup/death/magic/swing）。

---

## 0. 概念卡

| 项 | 内容 |
|---|---|
| 问题 | 引擎怎么把一堆声音同时放出来，而且音量可控？ |
| 一句话直觉 | 声音是**离散采样序列**；混音就是把多条序列按增益相加。 |
| 数学 | 采样定理、PCM 量化、混音求和、分贝、线性插值重采样。 |
| 代码 | `ParseWav`、`AudioSystem::Play`、`Update` 回收 voice。 |
| 实验 | `--audio-test`；游戏里开枪/命中/拾取/升级/死亡。 |
| 差距 | 没有 3D 空间音频（衰减/多普勒）、没有流式音乐、没有混响。 |

---

## 1. 声音的数学表示

### 1.1 采样

连续信号 `x(t)` 以 `fs` 采样得到 `x[n] = x(n/fs)`。
**奈奎斯特–香农采样定理**：要无失真重建，需

```
fs > 2 · f_max
```

CD 音质 `fs = 44100 Hz`，能表示到 22.05 kHz（人耳上限约 20 kHz）。

### 1.2 量化

16 bit PCM 把 `[-1, 1]` 映射到 `[-32768, 32767]`：

```
q[n] = round(x[n] · 32767)
```

量化误差 ≤ 1/2 LSB，信噪比约 `6.02 · B` dB（16 bit ≈ 96 dB）。

### 1.3 通道与字节率

```
byteRate = sampleRate · channels · bytesPerSample
duration = dataSize / byteRate
```

本工程只支持 PCM16 / PCM24 / float32——**不引入 Ogg 解码器**，因为 XAudio2 原生只吃 PCM。

---

## 2. WAV 文件结构

```
RIFF <size> WAVE
  fmt  <16>  formatTag, channels, sampleRate, byteRate, blockAlign, bitsPerSample
  data <N>   采样数据
```

`AudioSystem::Load` 按 chunk 遍历，只保留 `data` 段，并记录 `durationMs` 供回收使用。
**坑**：WAV 的 chunk 按 2 字节对齐，奇数长度要 `offset++`；`bitsPerSample` 在 fmt 块偏移 +14，
写到 +12 会读到 blockAlign（表现为「16 bit 变成 2 bit」，XAudio2 拒绝创建 voice）。

---

## 3. XAudio2 架构

```
IXAudio2 (引擎)
  └─ MasteringVoice（主输出，最终混音）
       └─ SourceVoice × N（每个音效一个）
            └─ XAUDIO2_BUFFER（指向 PCM 内存）
```

- `CreateSourceVoice(fmt)`：按格式建声部；
- `SubmitSourceBuffer`：提交一段 PCM；
- `SetVolume` / `SetFrequencyRatio`：音量、音高（变调）；
- 播放完要 `DestroyVoice`，否则声部泄漏。

本工程用一个固定数组记录 `(voice, remaining)`，在 `Update(dt)` 里倒计时回收，
并限制最多 48 个声部（超出就强制回收最老的）。

---

## 4. 混音与音量

### 4.1 求和

多条声音同时播放时：

```
y[n] = Σ g_i · x_i[n]
```

`g_i` 是每条声部的增益。叠加可能超过 `[-1,1]`，硬件会削波（clipping），
所以要有主音量与限幅。

### 4.2 分贝

```
dB = 20 · log10(g)
```

- 0 dB = 原始音量；
- −6 dB ≈ 一半振幅；
- 主音量滑条用线性 0..1，内部直接乘到 `g` 上（听感上更接近平方关系，工业上常用 `g²`）。

### 4.3 音高

`SetFrequencyRatio(r)` 会重采样，`r=2` 升八度。简单的重采样是线性插值：

```
x(t) ≈ (1−α)·x[n] + α·x[n+1]
```

---

## 5. 代码地图

| 功能 | 位置 |
|---|---|
| WAV 解析 | `platform/Audio.cpp` `ParseWav` |
| 播放/缓存 | `AudioSystem::Load` / `Play` |
| 声部回收 | `AudioSystem::Update` |
| 主音量/静音 | `SetMasterVolume` / `SetMuted` |
| 游戏触发点 | `game/Survivors.cpp` / `game/Survivors3D.cpp`（`PlaySfx`） |
| 素材许可 | `assets/audio/LICENSE-AUDIO.txt` + `docs/assets-credits.md` |

---

## 6. 实验

```bat
:: 依次播放 8 个音效
build\bin\ErMao.exe --audio-test --frames 200

:: 游戏里听：开枪 / 命中 / 拾取 / 升级 / 死亡
build\bin\ErMao.exe --demo2d
build\bin\ErMao.exe --demo3d
```

暂停菜单里有**音量滑条**与**静音**复选框。

---

## 7. 失败模式

| 现象 | 原因 | 排查 |
|---|---|---|
| 没有声音 | XAudio2 初始化失败 / 声卡独占 | 看日志「音频系统就绪」 |
| 声音全是噪声 | 采样率/位深填错 | 打印 `fmt` 的 4 个字段 |
| 只响一声 | voice 没回收/被覆盖 | 看 `ActiveVoices` |
| 播放卡顿 | 每帧新建 voice | 缓存 + 池化 |
| WAV 加载失败 | 压缩格式（ADPCM/浮点扩展） | 转成 PCM16 |

---

## 8. 练习（故意改坏）

1. 把 `bitsPerSample` 的偏移改成 +12：**预期**日志出现「2 bit」，XAudio2 创建 voice 失败、无声。
2. 把 `Update` 里的 `remaining` 永远不减：**预期**声部泄漏，`ActiveVoices` 一路涨到 48 后老声音被强杀。
3. 把 `SetMasterVolume` 的 `g` 改成 `g*g`：**预期**同一滑条位置明显更小（-6dB 处约 -12dB）。
4. 把 `Play` 的 pitch 固定为 2.0：**预期**所有音效升八度、变短。

---

## 9. 与工业音频的差距

| 项 | 本工程 | 工业（Wwise/FMOD） |
|---|---|---|
| 空间音频 | 无 | HRTF、衰减曲线、多普勒 |
| 总线/混响 | 单一主输出 | 总线、Snapshot、DSP 链 |
| 流式音乐 | 无（整段 PCM） | 流式解码、无缝循环 |
| 事件系统 | 直接按名字播放 | 事件 + 参数 + 随机容器 |

## 数学依据与参考资料

> 目的：不要求背下推导，但要知道这一讲**用的是什么数学工具**、去哪里查。

| 用到的原理 | 数学形式 / 名称 | 参考 |
|---|---|---|
| 采样定理 | fs > 2·f_max；x[n] = x(n/fs) | [AUDIO] |
| PCM 量化 | q[n] = round(x[n]·32767)，SNR ≈ 6.02·B dB | [AUDIO] |
| 混音 | y[n] = Σ g_i·x_i[n]；dB = 20·log10(g) | [AUDIO] |
| 重采样/变调 | 线性插值 x(t) ≈ (1−α)x[n] + αx[n+1] | [AUDIO] |
