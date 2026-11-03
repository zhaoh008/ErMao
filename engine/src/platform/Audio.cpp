// ErMao Engine - 音频子系统实现（XAudio2 + WAV/PCM）
#include "platform/Audio.h"

#include "core/FileSystem.h"
#include "core/Log.h"

#include <xaudio2.h>

#include <algorithm>
#include <cstring>

namespace erm {

namespace {

bool ReadU16(const u8* p, u32 size, u32 offset, u16& value) {
    if (offset + 2 > size) return false;
    std::memcpy(&value, p + offset, 2);
    return true;
}

bool ReadU32(const u8* p, u32 size, u32 offset, u32& value) {
    if (offset + 4 > size) return false;
    std::memcpy(&value, p + offset, 4);
    return true;
}

// 解析 RIFF/WAVE：只需要 fmt + data
bool ParseWav(std::vector<u8>& bytes, AudioSystem::Sound& sound) {
    (void)bytes;
    if (bytes.size() < 44) return false;
    if (std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0) return false;

    u32 offset = 12;
    bool gotFormat = false;
    while (offset + 8 <= bytes.size()) {
        char id[5] = {0};
        std::memcpy(id, bytes.data() + offset, 4);
        u32 chunkSize = 0;
        ReadU32(bytes.data(), static_cast<u32>(bytes.size()), offset + 4, chunkSize);
        offset += 8;
        if (offset + chunkSize > bytes.size()) chunkSize = static_cast<u32>(bytes.size()) - offset;

        if (std::memcmp(id, "fmt ", 4) == 0 && chunkSize >= 16) {
            u16 fmtTag = 0, channels = 0, bits = 0;
            u32 sampleRate = 0, byteRate = 0;
            ReadU16(bytes.data(), static_cast<u32>(bytes.size()), offset + 0, fmtTag);
            ReadU16(bytes.data(), static_cast<u32>(bytes.size()), offset + 2, channels);
            ReadU32(bytes.data(), static_cast<u32>(bytes.size()), offset + 4, sampleRate);
            ReadU32(bytes.data(), static_cast<u32>(bytes.size()), offset + 8, byteRate);
            ReadU16(bytes.data(), static_cast<u32>(bytes.size()), offset + 14, bits);
            sound.formatTag = fmtTag;
            sound.channels = channels;
            sound.sampleRate = sampleRate;
            sound.bitsPerSample = bits;
            gotFormat = true;
        } else if (std::memcmp(id, "data", 4) == 0) {
            // 只保留 data 块的样本，避免把整个文件交给 XAudio2
            std::vector<u8> samples(bytes.begin() + offset, bytes.begin() + offset + chunkSize);
            const u32 blockAlign = static_cast<u32>(sound.channels) * (sound.bitsPerSample / 8u);
            if (blockAlign > 0) {
                sound.durationMs = static_cast<u32>(1000ull * samples.size() / (sound.sampleRate * blockAlign));
            }
            bytes.swap(samples);
            return gotFormat;
        }
        offset += chunkSize;
        if (chunkSize & 1u) ++offset; // WAV 块按 2 字节对齐
    }
    return false;
}

} // namespace

AudioSystem& AudioSystem::Get() {
    static AudioSystem instance;
    return instance;
}

bool AudioSystem::Init() {
    if (m_initialized) return true;
    if (m_failed) return false;

    if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) {
        // 已初始化过也继续
    }

    if (FAILED(XAudio2Create(&m_engine, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        LogWarn("XAudio2 创建失败，音频禁用");
        m_failed = true;
        return false;
    }
    if (FAILED(m_engine->CreateMasteringVoice(&m_master))) {
        LogWarn("XAudio2 主混音声部创建失败，音频禁用");
        m_engine->Release();
        m_engine = nullptr;
        m_failed = true;
        return false;
    }
    m_master->SetVolume(m_masterVolume);
    m_initialized = true;
    LogInfo("音频系统就绪（XAudio2，主音量 %.2f）", m_masterVolume);
    return true;
}

const AudioSystem::Sound* AudioSystem::Load(const std::string& name) {
    auto it = m_sounds.find(name);
    if (it != m_sounds.end()) return &it->second;

    const std::string path = FileSystem::AssetPath("audio/" + name + ".wav");
    std::vector<u8> bytes;
    if (!FileSystem::ReadBinaryFile(path, bytes)) {
        LogWarn("音频文件不存在: %s", path.c_str());
        return nullptr;
    }

    Sound sound;
    if (!ParseWav(bytes, sound)) {
        LogWarn("WAV 解析失败: %s", path.c_str());
        return nullptr;
    }
    sound.bytes = std::move(bytes);
    auto [inserted, ok] = m_sounds.emplace(name, std::move(sound));
    m_names.push_back(name);
    LogInfo("音频加载: %s（%u Hz / %u ch / %u bit / %u ms）", name.c_str(), inserted->second.sampleRate,
            inserted->second.channels, inserted->second.bitsPerSample, inserted->second.durationMs);
    return ok ? &inserted->second : nullptr;
}

void AudioSystem::Play(const std::string& name, float volume, float pitch) {
    if (!m_initialized || !m_engine || !m_sfxEnabled) return;
    const Sound* sound = Load(name);
    if (!sound || sound->bytes.empty()) return;

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = sound->formatTag;
    fmt.nChannels = sound->channels;
    fmt.nSamplesPerSec = sound->sampleRate;
    fmt.wBitsPerSample = sound->bitsPerSample;
    fmt.nBlockAlign = static_cast<WORD>(fmt.nChannels * (fmt.wBitsPerSample / 8));
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;
    fmt.cbSize = 0;

    IXAudio2SourceVoice* voice = nullptr;
    if (FAILED(m_engine->CreateSourceVoice(&voice, &fmt))) return;

    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(sound->bytes.size());
    buffer.pAudioData = sound->bytes.data();
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    if (FAILED(voice->SubmitSourceBuffer(&buffer))) {
        voice->DestroyVoice();
        return;
    }

    const float finalVolume = m_muted ? 0.0f : volume * m_sfxVolume * m_masterVolume;
    voice->SetVolume(finalVolume);
    voice->SetFrequencyRatio(std::clamp(pitch, 0.25f, 4.0f));
    voice->Start(0);

    m_voices.push_back({voice, static_cast<float>(sound->durationMs) / 1000.0f + 0.25f});
    if (m_voices.size() > 48) {
        // 上限：强制回收最老的
        m_voices.front().voice->DestroyVoice();
        m_voices.erase(m_voices.begin());
    }
}

void AudioSystem::PlayMusic(const std::string& name, float volume) {
    if (!m_initialized || !m_engine || !m_musicEnabled) return;
    if (m_music && m_musicName == name) {
        m_musicVolume = std::clamp(volume, 0.0f, 1.0f);
        m_music->SetVolume(m_muted ? 0.0f : m_musicVolume * m_masterVolume);
        return;
    }
    StopMusic();

    const Sound* sound = Load(name);
    if (!sound) return;

    WAVEFORMATEX fmt{};
    fmt.wFormatTag = sound->formatTag;
    fmt.nChannels = sound->channels;
    fmt.nSamplesPerSec = sound->sampleRate;
    fmt.wBitsPerSample = sound->bitsPerSample;
    fmt.nBlockAlign = static_cast<WORD>(fmt.nChannels * (fmt.wBitsPerSample / 8));
    fmt.nAvgBytesPerSec = fmt.nSamplesPerSec * fmt.nBlockAlign;

    if (FAILED(m_engine->CreateSourceVoice(&m_music, &fmt))) return;
    XAUDIO2_BUFFER buffer{};
    buffer.AudioBytes = static_cast<UINT32>(sound->bytes.size());
    buffer.pAudioData = sound->bytes.data();
    buffer.LoopCount = XAUDIO2_LOOP_INFINITE;
    buffer.Flags = XAUDIO2_END_OF_STREAM;
    m_music->SubmitSourceBuffer(&buffer);
    m_musicVolume = std::clamp(volume, 0.0f, 1.0f);
    m_musicName = name;
    m_music->SetVolume(m_muted ? 0.0f : m_musicVolume * m_masterVolume);
    m_music->Start(0);
}

void AudioSystem::StopMusic() {
    if (m_music) {
        m_music->Stop(0);
        m_music->DestroyVoice();
        m_music = nullptr;
    }
    m_musicName.clear();
}

void AudioSystem::Update(float dt) {
    if (!m_initialized) return;
    for (size_t i = 0; i < m_voices.size();) {
        m_voices[i].remaining -= dt;
        if (m_voices[i].remaining <= 0.0f) {
            m_voices[i].voice->DestroyVoice();
            m_voices.erase(m_voices.begin() + static_cast<long long>(i));
        } else {
            ++i;
        }
    }
}

void AudioSystem::SetMasterVolume(float volume) {
    m_masterVolume = std::clamp(volume, 0.0f, 1.0f);
    if (m_master) m_master->SetVolume(m_muted ? 0.0f : m_masterVolume);
    if (m_music) m_music->SetVolume(m_muted ? 0.0f : m_musicVolume * m_masterVolume);
}

void AudioSystem::SetMuted(bool muted) {
    m_muted = muted;
    if (m_master) m_master->SetVolume(m_muted ? 0.0f : m_masterVolume);
    if (m_music) m_music->SetVolume(m_muted ? 0.0f : m_musicVolume * m_masterVolume);
}

void AudioSystem::SetMusicEnabled(bool enabled) {
    m_musicEnabled = enabled;
    if (!enabled) StopMusic();
}

void AudioSystem::SetSfxEnabled(bool enabled) {
    m_sfxEnabled = enabled;
    if (!enabled) {
        for (auto& voice : m_voices) voice.voice->DestroyVoice();
        m_voices.clear();
    }
}

void AudioSystem::SetMusicVolume(float volume) {
    m_musicVolume = std::clamp(volume, 0.0f, 1.0f);
    if (m_music) m_music->SetVolume(m_muted ? 0.0f : m_musicVolume * m_masterVolume);
}

void AudioSystem::SetSfxVolume(float volume) { m_sfxVolume = std::clamp(volume, 0.0f, 1.0f); }

void AudioSystem::Shutdown() {
    if (!m_initialized) return;
    StopMusic();
    for (auto& v : m_voices) v.voice->DestroyVoice();
    m_voices.clear();
    if (m_master) {
        m_master->DestroyVoice();
        m_master = nullptr;
    }
    if (m_engine) {
        m_engine->Release();
        m_engine = nullptr;
    }
    m_sounds.clear();
    m_musicName.clear();
    m_initialized = false;
    LogInfo("音频系统关闭");
}

} // namespace erm
