// ErMao Engine - 音频子系统（GAMES104 第 12/13 讲的扩展）
//
// 设计：
//   * Windows 上用 XAudio2（系统自带，无需第三方库）；
//   * 只支持 WAV/PCM（16bit / 32bit float），避免引入 Ogg/Vorbis 解码器；
//   * 一次性音效 + 单条循环背景音乐，主音量/静音可调；
//   * 全局单例 AudioSystem::Get()，玩法组件直接 Play("shoot")。
#pragma once

#include "core/Types.h"

#include <string>
#include <unordered_map>
#include <vector>

struct IXAudio2;
struct IXAudio2MasteringVoice;
struct IXAudio2SourceVoice;

namespace erm {

class AudioSystem {
public:
    struct Sound {
        std::vector<u8> bytes;
        u16             formatTag = 0;
        u16             channels = 0;
        u32             sampleRate = 0;
        u16             bitsPerSample = 0;
        u32             durationMs = 0;
    };

    static AudioSystem& Get();

    bool Init();
    void Shutdown();

    void Play(const std::string& name, float volume = 1.0f, float pitch = 1.0f);
    void PlayMusic(const std::string& name, float volume = 0.5f);
    void StopMusic();

    void  Update(float dt);
    void  SetMasterVolume(float volume);
    float MasterVolume() const { return m_masterVolume; }
    void  SetMuted(bool muted);
    bool  Muted() const { return m_muted; }

    // 背景音乐 / 游戏音效可分别开关和调音量（2D 幸存者的设置面板用）
    void  SetMusicEnabled(bool enabled);
    bool  MusicEnabled() const { return m_musicEnabled; }
    void  SetSfxEnabled(bool enabled);
    bool  SfxEnabled() const { return m_sfxEnabled; }
    void  SetMusicVolume(float volume);
    float MusicVolume() const { return m_musicVolume; }
    void  SetSfxVolume(float volume);
    float SfxVolume() const { return m_sfxVolume; }
    bool  IsMusicPlaying() const { return m_music != nullptr; }
    const std::string& CurrentMusic() const { return m_musicName; }

    bool  Ready() const { return m_engine != nullptr; }
    u32   ActiveVoices() const { return static_cast<u32>(m_voices.size()); }
    const std::vector<std::string>& LoadedNames() const { return m_names; }

private:
    AudioSystem() = default;
    ~AudioSystem() = default;

    struct Voice {
        IXAudio2SourceVoice* voice = nullptr;
        float                remaining = 0.0f;
    };

    const Sound* Load(const std::string& name);

    IXAudio2*               m_engine = nullptr;
    IXAudio2MasteringVoice* m_master = nullptr;
    float                   m_masterVolume = 0.8f;
    bool                    m_muted = false;

    std::vector<Voice> m_voices;

    IXAudio2SourceVoice* m_music = nullptr;
    float                m_musicVolume = 0.5f;
    std::string          m_musicName;
    bool                 m_musicEnabled = true;
    bool                 m_sfxEnabled = true;
    float                m_sfxVolume = 1.0f;

    std::unordered_map<std::string, Sound> m_sounds;
    std::vector<std::string>                m_names;
    bool                                    m_initialized = false;
    bool                                    m_failed = false;
};

inline void PlaySfx(const std::string& name, float volume = 1.0f, float pitch = 1.0f) {
    AudioSystem::Get().Play(name, volume, pitch);
}

} // namespace erm