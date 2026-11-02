# 第 25 讲：音频——PCM、混音与 XAudio2【进阶·已实现】

> 游戏不能只有画面。这一讲把「声音」的最小闭环打通：读 WAV → 交给 XAudio2 → 混音输出。
> 代码：`engine/src/platform/Audio.h/.cpp`；素材：`engine/assets/audio/*.wav`（许可见
> `engine/assets/audio/LICENSE-AUDIO.txt` 与 `文档/版本文档/发布与工程.md`；来源 OpenGameArt，作者 SubspaceAudio / artisticdude，CC0 1.0）。
> 状态：**已实现**（PCM 播放、多声部混音、主音量/静音、音高）；背景音乐接口**部分实现**
> （`PlayMusic`/`StopMusic` 存在，但仓库里没有关卡调用）；3D 衰减、流式音乐、DSP 效果**尚未实现**。
> 运行：`--audio-test` 依次播放 click/shoot/hit/pickup/levelup/death/magic/swing。
> 本环境是 Linux，只能读代码；第 10 节实验**未实际运行**。术语见 [术语表](术语表.md)。

---

## 0. 先听一局游戏

3D 幸存者里，角色每 0.7 秒自动射一发；光弹命中时有火花、**玩家**死亡有死亡音、捡宝石有拾取音、
升级有升级音。几秒钟内这些声音会**叠在一起**：射击声还没放完，拾取声又响了。
导演的暂停菜单代码里还有音量滑条和静音（当前点不到，见 §10）。

三个可观察的问题：

1. **一个 `.wav` 文件怎样变成扬声器里的声音？** 文件头里到底存了什么？
2. **多个声音怎样同时响？** 为什么不是「后一个打断前一个」？
3. **音量滑条和音高怎么实现？** 为什么调小音量听起来不是线性变小？为什么有的声音会被截断或爆？

> 本工程没有「背景音乐」在跑：`AudioSystem::PlayMusic` 提供了无限循环接口，但**没有任何关卡调用**
> （仓库里搜不到调用点）。实际同时响的是多个一次性的音效，不是「BGM + 音效」。

## 1. 概念卡

| 项 | 内容 |
|---|---|
| 问题 | 引擎怎么把一堆声音同时放出来，而且音量可控？ |
| 一句话直觉 | 声音是**离散采样序列**；混音就是把多条序列按增益相加。 |
| 数学 | 采样定理、PCM 量化、混音求和、分贝、音高比与重采样。 |
| 代码 | `ParseWav`、`AudioSystem::Load/Play/Update`、`PlaySfx`。 |
| 实验 / 差距 | `--audio-test`；无 3D 空间音频、无流式音乐、无混响、无总线。 |

## 2. 最简闭环：读 WAV → 送进声卡

**概念伪代码（不是仓库实现）**

```cpp
XAudio2Create(&engine);
engine->CreateMasteringVoice(&master);         // 最终混音，接扬声器
fmt  = { formatTag, channels, sampleRate, bitsPerSample };  // 从 WAV 的 fmt 块读
data = 文件里 "data" 块之后的字节;
engine->CreateSourceVoice(&voice, &fmt);       // 每个声音一个 source voice
XAUDIO2_BUFFER buf{}; buf.AudioBytes = data.size(); buf.pAudioData = data.data();
voice->SubmitSourceBuffer(&buf);
voice->Start(0);
```

这段代码就能出声。XAudio2 是 Windows SDK 自带的音频 API（见 [XAUDIO2]），不需要第三方库；代价是只能吃它认识的格式，主要是 PCM / float32。
本工程**不引入 Ogg/Vorbis 解码器**，所以 `AudioSystem` 只处理 WAV。

## 3. 最简版在哪里坏掉

**3.1 格式字段一错，声音全变噪声。** WAV 的 `fmt ` 块里，`bitsPerSample` 在块内偏移 **+14**。
如果按直觉写到 +12，读到的是 `blockAlign`（每帧字节数）：
单声道 16 bit 的 `blockAlign=2`，于是「16 bit」变成「2 bit」，XAudio2 拒绝创建 voice 或输出噪声。
同理，声道数写错会让左右声道交错的数据被当成单声道，速度、音高全错。

**3.2 一个声部只能放一个声音。** 只有一个 source voice 时，第二次 `SubmitSourceBuffer`
会把前一个替换/排队，听起来就是「只响一个」。要同时响，必须**每个正在播放的声音一个声部**，
让 mastering voice 在最后一步把它们加起来。

**3.3 音量不是线性的。** 人耳对响度的感知接近对数。滑条从 1.0 拉到 0.5，振幅减半，
听感只下降约 6 dB，而不是「小一半」。另外如果主音量在 source voice 和 mastering voice 上各乘一次，
实际会变成平方。

**3.4 音高与时长不匹配。** `SetFrequencyRatio(2.0)` 让声音快一倍（升八度），
但「什么时候回收这个声部」如果还按原始时长算，就会提前把还在响的声音杀掉。

**3.5 延迟与爆音。** 每次播放都 `CreateSourceVoice` 有创建开销；在声音还没播完时 `DestroyVoice`
会直接切断波形，产生「咔哒」；多个声音叠加超过 `[-1,1]` 会削波（clipping）。

## 4. 正式方案

### 4.1 PCM：把连续波形变成整数序列（采样与量化）

**采样**：`x[n] = x(n/fs)`。奈奎斯特–香农要求 `fs > 2·f_max`；CD 的 `fs=44100 Hz` 能表示到
22.05 kHz（人耳上限约 20 kHz），本仓库所有音频都是 44100 Hz。**量化**：16 bit 把 `[-1,1]` 映射到
`[-32768,32767]`（`q[n]=round(x[n]·32767)`），24 bit 用 `8388607`；量化信噪比约每比特 6.02 dB
（满幅正弦更精确的是 `6.02B+1.76`），16 bit ≈ **96.3 dB**，24 bit ≈ **144.5 dB**。

**字节率与时长**：`blockAlign = channels × bits/8`，`byteRate = sampleRate × blockAlign`，
`duration = dataBytes / byteRate`。本仓库部分真实文件（读 WAV 头得到）：

| 文件 | 声道 | 位深 | 采样率 | data 字节 | 时长 |
|---|---|---|---|---|---|
| `click.wav` | 1 | 16 | 44100 | 30968 | 0.351 s |
| `shoot.wav` | 1 | 16 | 44100 | 10240 | 0.116 s |
| `magic.wav` | 2 | **24** | 44100 | 231528 | 0.875 s |
| `swing.wav` | 2 | **24** | 44100 | 60636 | 0.229 s |

`LICENSE-AUDIO.txt` 写「PCM 16bit」，但 `magic`/`swing` 实际是 **24 bit 立体声**（`blockAlign=6`）；
不影响播放（XAudio2 支持 24 bit PCM），但排查时别被许可说明带偏。

### 4.2 WAV 结构（格式示意）与本工程的解析

```
偏移 0   "RIFF" | 文件长度 | "WAVE"
偏移 12  chunk id (4) | chunk size (4) | 数据...
  "fmt " 数据: formatTag(+0,u16) channels(+2,u16) sampleRate(+4,u32)
               byteRate(+8,u32) blockAlign(+12,u16) bitsPerSample(+14,u16)
  "data" 数据: 采样字节
```

`AudioSystem::Load` 读整个文件，`ParseWav` 逐 chunk 找 `fmt ` 和 `data`，遇到 `data` 就把
**只有样本**的那段 `swap` 进 `bytes` 并返回；chunk 长度为奇数时偏移 `+1`（2 字节对齐）。

**源码节选：`platform/Audio.cpp` `ParseWav`（有删节）**

```cpp
ReadU16(bytes.data(), size, offset + 0, fmtTag);
ReadU16(bytes.data(), size, offset + 2, channels);
ReadU32(bytes.data(), size, offset + 4, sampleRate);
ReadU32(bytes.data(), size, offset + 8, byteRate);
ReadU16(bytes.data(), size, offset + 14, bits);
...
} else if (std::memcmp(id, "data", 4) == 0) {
    std::vector<u8> samples(bytes.begin() + offset, bytes.begin() + offset + chunkSize);
    const u32 blockAlign = static_cast<u32>(sound.channels) * (sound.bitsPerSample / 8u);
    if (blockAlign > 0) sound.durationMs = static_cast<u32>(1000ull * samples.size() / (sound.sampleRate * blockAlign));
    bytes.swap(samples);
    return gotFormat;
}
offset += chunkSize;
if (chunkSize & 1u) ++offset;   // WAV 块按 2 字节对齐
```

三个实现事实：① **假设 fmt 在前**——若 `data` 先出现，`channels`/`bits` 还是 0，
`durationMs=0` 且 `gotFormat=false` 返回失败；② **durationMs 是整数除法**：
`1000×30968/(44100×2)=351.1` → **351 ms**；③ **不校验 formatTag**：`wFormatTag` 原样来自文件、
`cbSize=0`，也不复制扩展字节，所以带 cbSize 的压缩格式（ADPCM）会创建 voice 失败，PCM/float32 能过。
「只支持 PCM」是**结果**，不是代码里的检查。

### 4.3 XAudio2 的 voice 图（示意）与 Play

```
IXAudio2（引擎）→ MasteringVoice（主输出，最终混音，默认按设备采样率）
                → SourceVoice × N（每个音效一个）→ XAUDIO2_BUFFER（PCM 内存，END_OF_STREAM）
```

**源码节选：`platform/Audio.cpp` `AudioSystem::Play`**

```cpp
if (FAILED(m_engine->CreateSourceVoice(&voice, &fmt))) return;
XAUDIO2_BUFFER buffer{};
buffer.AudioBytes = static_cast<UINT32>(sound->bytes.size());
buffer.pAudioData = sound->bytes.data();
buffer.Flags = XAUDIO2_END_OF_STREAM;
if (FAILED(voice->SubmitSourceBuffer(&buffer))) { voice->DestroyVoice(); return; }
voice->SetVolume(m_muted ? 0.0f : volume * m_masterVolume);
voice->SetFrequencyRatio(std::clamp(pitch, 0.25f, 4.0f));
voice->Start(0);
m_voices.push_back({voice, static_cast<float>(sound->durationMs) / 1000.0f + 0.25f});
```

`fmt` 由 WAV 字段拼成（`nBlockAlign=channels×bits/8`、`nAvgBytesPerSec=sampleRate×nBlockAlign`）。
`END_OF_STREAM` 表示最后一段；source 与 mastering 采样率不同时 XAudio2 自动重采样，所以 44100 的
文件在 48000 设备上也能放。`Play` 每次都新建 voice，**没有 voice 池**。

### 4.4 混音（Mixing）、音量与分贝（dB）

**混音就是求和**：`y[n] = Σ g_i·x_i[n]`，`g_i` 是各 source voice 的 `SetVolume`，
mastering voice 再乘一次自己的增益。`|y[n]|>1` 时最终转整数样本会**削波**，听感是破音。

**增益与分贝**：`dB = 20·log10(g)`。`g=1.0→0 dB`、`0.8→−1.94 dB`、`0.5→−6.02 dB`、
`0.35→−9.12 dB`、`0.25→−12.04 dB`。所以「滑条拉到一半」是 −6 dB，不是「音量一半」。工业上常把滑条值 `s` 映射成 `s²` 再当增益
让听感更线性；**本工程没做**，`SetMasterVolume` 直接 `clamp(volume,0,1)`。

**一个值得注意的细节**：主音量写进了**两处**——`Init`/`SetMasterVolume` 设 mastering voice，
`Play` 又设 `voice->SetVolume(volume × master)`。按 XAudio2 语义两级都作用，实际增益是
`volume × master²`：滑条 0.5 时是 0.25（−12 dB），不是 −6 dB。这是代码推导，未在 Windows 实测；
若本意是线性滑条，应只设一处。副作用：**已有声部烘死了旧 master**，调完滑条后新老声音比例会短暂不一致。

**静音**：`SetMuted(true)` 把 mastering voice 设 0，已存在的 source voice 不用逐个改；
`Play` 里也会检查 `m_muted`。

### 4.5 音高：频率比与重采样（Resampling）

`SetFrequencyRatio(r)` 让声音以 `r` 倍速度播放，等价于把读取步长从 1 改成 `r`：
`y[n]=x(n·r)`。`r=2` 升八度，半音数 `= 12·log2(r)`；本工程把 `r` 夹在 `[0.25,4]`。
读取位置一般不是整数，需要插值，最简是线性插值 `x(t)≈(1−α)x[n]+αx[n+1]`（`α=t−n`）；
XAudio2 内部会做重采样（具体滤波器未公开，不能假定它就是线性插值），这里只把它当理解「变速变调」的模型。

**回收与音高不匹配（真实缺陷）**：声部寿命按 `durationMs/1000+0.25` 算，**没有除以 `r`**。
`magic` 0.875 s 设 `pitch=0.5` 实际要放 1.75 秒，`Update` 在 1.125 秒就 `DestroyVoice`，结尾被
**提前切断**；反过来 `pitch=4` 时 0.22 秒就播完，声部还占着到 1.125 秒。正确做法是按 `duration/r`
估算，或查 `GetState` 的 `SamplesPlayed`。

### 4.6 声部管理与音乐

**源码节选：`platform/Audio.cpp` `AudioSystem::Update`**

```cpp
for (size_t i = 0; i < m_voices.size();) {
    m_voices[i].remaining -= dt;
    if (m_voices[i].remaining <= 0.0f) {
        m_voices[i].voice->DestroyVoice();
        m_voices.erase(m_voices.begin() + static_cast<long long>(i));
    } else { ++i; }
}
```

- `m_voices` 是 `std::vector<Voice>`，`Voice{IXAudio2SourceVoice*, float remaining}`；
- 上限 **48**：超过就销毁**最老的**（`front`）再 `erase(begin)`，爆炸/连射时不会无限创建，
  代价是老声音被硬切；没有淡出、没有优先级；
- 音乐走单独的 `m_music`：`PlayMusic` 用 `LoopCount=XAUDIO2_LOOP_INFINITE` 整段循环，
  `StopMusic` 停止并销毁；`m_musicVolume` 与主音量相乘，同样存在「主音量乘两次」的问题。

## 5. 代码地图

| 功能 | 位置 |
|---|---|
| WAV 解析 | `platform/Audio.cpp` `ParseWav`（匿名命名空间） |
| 加载/缓存 | `AudioSystem::Load`（`m_sounds` + `m_names`） |
| 播放/音量/音高 | `AudioSystem::Play` |
| 声部回收 | `AudioSystem::Update` |
| 主音量/静音 | `AudioSystem::SetMasterVolume` / `SetMuted` |
| 背景音乐 / 自检 | `AudioSystem::PlayMusic` / `StopMusic`（无调用点）；`runtime/Engine.cpp` 的 `--audio-test` |
| 玩法触发 / 许可 | `PlaySfx` → `Survivors3D.cpp` / `Survivors.cpp`；`LICENSE-AUDIO.txt` |

`PlaySfx(name, volume=1.0f, pitch=1.0f)` 只是 `AudioSystem::Get().Play(...)` 的薄包装，
所以调用点不持有 voice，生命周期完全由 `AudioSystem` 管。

## 6. 一个声音的完整演算

以 `click.wav` 为例：`PCM`、`channels=1`、`sampleRate=44100`、`bits=16`、data 30968 字节。

**第一步：帧数、时长与回收时间**

```
blockAlign = 1 × 16/8 = 2        byteRate = 44100 × 2 = 88200
帧数 = 30968 / 2 = 15484         时长 = 15484 / 44100 = 0.35111 s（代码取整 351 ms）
remaining = 351/1000 + 0.25 = 0.601 s
```

**第二步：同时响两个声音**。设某时刻 `click` 样本 `x₁=0.4`、`shoot` 样本 `x₂=−0.2`；
`PlaySfx("shoot", 0.35, 0.9~1.1)`、`click` 用 1.0，主音量默认 0.8。按**单处**增益：

```
g_click = 1.0×0.8 = 0.8          g_shoot = 0.35×0.8 = 0.28
y = 0.8×0.4 + 0.28×(−0.2) = 0.32 − 0.056 = 0.264   （16 bit ≈ 8650）
```

`0.264` 没有削波。若主音量真的乘两次（§4.4），`g_click=0.64`、`g_shoot=0.224`，
`y = 0.256−0.0448 = 0.2112`，小约 1.9 dB——**主音量乘几次会直接改变混音结果**。

**第三步：分贝核对**：`20log10(0.28)=−11.06 dB`、`20log10(0.224)=−13.00 dB`、`20log10(0.8)=−1.94 dB`。

**第四步：音高与时长**。`shoot` pitch=1.0 时 0.116 s；pitch=0.5 时实际 0.232 s，
`remaining=0.366` 够用。但 `magic`（0.875 s）取 0.5 时实际 1.75 秒、`remaining=1.125` 秒，
结尾会被切断（§4.5）。

## 7. 声音在游戏里什么时候响

| 调用点 | 音效 | 参数 | 说明 |
|---|---|---|---|
| `Survivors3DPlayerComponent::FireAtNearestEnemy` | `shoot` | 0.35，pitch 0.9~1.1 | 每次自动射击 |
| `Survivors3DPlayerComponent::AddXp` | `levelup` | 0.6 | 每次升级（while 循环里可能连响） |
| `Survivors3DGemComponent::OnUpdate` | `pickup` | 0.45，pitch 0.95~1.15 | 拾取宝石 |
| `Survivors3DDirector::Update` | `death` | 0.6 | 玩家死亡，切 GameOver |
| 两个开始按钮 / `--audio-test` | `click`；8 个全测 | 1.0；0.7 | 2D/3D 主菜单；每 18 帧一个 |

**2D 版没有战斗音效**：`Survivors.cpp` 只在开始按钮里 `PlaySfx("click")`，射击/受击/拾取都不发声；
`hit.wav`、`magic.wav`、`swing.wav` 在游戏里**从未被调用**，只在 `--audio-test` 里出现。
注意：这套战斗音效在 3D 幸存者里成立，2D 版不成立；不要按 3D 的调用点去 2D 代码里找。

## 8. 尚未实现 / 部分实现（如实标注）

| 能力 | 状态 | 说明 |
|---|---|---|
| PCM WAV 播放 | **已实现** | 16/24 bit、单/双声道、float32 由 XAudio2 支持 |
| 多声部混音 | **已实现** | 每个音效一个 source voice，最多 48 个 |
| 主音量 / 静音 | **已实现** | 但主音量乘了两次（§4.4），且滑条不是感知线性 |
| 音高 | **已实现** | `SetFrequencyRatio`，夹在 0.25~4 |
| 背景音乐 | **部分实现** | `PlayMusic` 无限循环可用，但无调用点、无淡入淡出、无无缝切换 |
| 3D 空间音频 | **尚未实现** | 没有 listener、距离衰减、方位声像、多普勒；`PlaySfx` 只有 name/volume/pitch |
| 流式音乐 | **尚未实现** | `PlayMusic` 把整段 PCM 一次性提交，长音乐要全在内存里 |
| DSP / 总线 / 混响 | **尚未实现** | 只有 mastering → source 两级，没有效果链、没有 snapshot |
| 事件系统 | **尚未实现** | 直接按名字播放，没有随机容器、参数、优先级 |

## 9. 失败模式与排查

| 现象 | 原因 | 排查入口 |
|---|---|---|
| 没有声音 | XAudio2 初始化失败 / 声卡独占 / 未初始化 | 看日志「音频系统就绪」；`Play` 在 `!m_initialized` 时直接 return |
| 全是噪声/速度不对 | 声道、位深、采样率填错（常见：bits 读成 +12 的 blockAlign） | 打印 `fmt` 的 5 个字段，对照 WAV 头 |
| 只响一个 | 复用了同一个 source voice / 声部被 48 上限挤掉 | 看日志里的 `活跃 voice`；`ActiveVoices()` |
| 结尾被切断 | `remaining` 按原始时长算，pitch<1 时实际更长 | §4.5，按 `duration/pitch` 估算 |
| 调音量后新老声音比例怪 | 主音量乘两次 + 旧 voice 烘死旧值 | §4.4 |
| 很多声音一起爆音 | 求和超过 `[-1,1]` | 降 `volume` 或加限幅 |
| WAV 加载失败 | `data` 在 `fmt` 前 / 压缩格式带 cbSize / 文件 <44 字节 | `ParseWav` 的三个前提 |

## 10. 实验（本次未实际运行）

本环境是 Linux，仓库只支持 Windows/D3D11，以下命令**没有在本机执行**。

```bat
:: 依次播放 8 个音效（每 18 帧一个）
build\bin\ErMao.exe --audio-test --frames 200

:: 游戏里听（3D 才有战斗音效）
build\bin\ErMao.exe --demo3d
build\bin\ErMao.exe --demo2d
```

1. **确认加载**：日志 `音频加载: name（Hz / ch / bit / ms）` 里 `click` 预期
   `44100 Hz / 1 ch / 16 bit / 351 ms`，`magic` 预期 `2 ch / 24 bit / 875 ms`。
2. **数声部**：日志「播放 xxx（活跃 voice N）」在播放期间递增，随后按 `remaining` 逐个回收；
   若只增不减直到 48 并硬切，说明 `Update` 没被调用或 `remaining` 不减。
3. **音量**：滑条只在导演的暂停菜单里，而 `State::Paused` 没有赋值点（第 23 讲已核对），
   所以**当前版本实际点不到滑条**——UI 与实现的偏差，不是音频问题。
4. **音高**：把 `pickup` 的 pitch 固定成 2.0，预期升八度、时长减半；固定成 0.5 时注意是否被提前切断。

## 11. 练习与自测

**练习（先猜再改）**：① 把 `bitsPerSample` 的偏移改成 +12——预期读到 `blockAlign`（单声道 16 bit
时是 2），日志位深变成 2，`CreateSourceVoice` 失败；② 把 `Update` 的 `remaining` 永远不减——
预期声部泄漏，`ActiveVoices` 涨到 48 后老声音被强杀；③ 把 `SetMasterVolume` 改成写入 `g*g`——
在已有「乘两次」的基础上会变成四次，滑条 0.5 时约 −24 dB；④ 把 `Play` 的 pitch 固定为 2.0——
预期所有音效升八度、变短。

**自测题（答案在小节）**：① 为什么 `fs` 要大于 `2·f_max`？→ §4.1；② 16 bit 的理论信噪比是多少？
→ §4.1（`6.02×16≈96.3 dB`）；③ `click.wav` 为什么是 351 ms 而不是 351.1 ms？→ §4.2（整数除法）；
④ 主音量 0.5 实际是 −6 dB 还是 −12 dB？→ §4.4（本工程乘两次，−12 dB）；⑤ pitch=2 是几度？
→ §4.5（八度，`12·log2(2)=12` 个半音）；⑥ 为什么 2D 版听不到射击声？→ §7（没接 `PlaySfx`）。

## 12. 与工业音频的差距

| 项 | 本工程 | 工业（Wwise/FMOD） |
|---|---|---|
| 空间音频 | 无 | HRTF、距离衰减、方位声像、多普勒 |
| 总线/混响 | 单一主输出 | 总线、Snapshot、DSP 链 |
| 流式音乐 / 事件 | 无（整段 PCM）；直接按名字播放 | 流式解码、无缝循环；事件 + 参数 + 随机容器 + 优先级 |
| 声部管理 | 固定 48、最老硬切 | 虚拟 voice、优先级、stealing 策略 |

## 数学依据与参考资料

> 目的：不要求背下推导，但要知道这一讲**用的是什么数学工具**、去哪里查。

| 用到的原理 | 数学形式 / 名称 | 参考 |
|---|---|---|
| 采样定理 | `fs > 2·f_max`；`x[n] = x(n/fs)` | [AUDIO] 离散时间信号 |
| PCM 量化 | `q[n] = round(x[n]·32767)`；`SNR ≈ 6.02·B` dB | [AUDIO] |
| 混音 | `y[n] = Σ g_i·x_i[n]`；削波条件 `abs(y)>1` | [AUDIO]（含 Pirkle 混音章节） |
| 分贝 | `dB = 20·log10(g)`；半音 `= 12·log2(r)` | [AUDIO] |
| 重采样 / 变调 | `y[n]=x(n·r)`；线性插值 `(1−α)x[n]+αx[n+1]` | [AUDIO] 重采样 |
