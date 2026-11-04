// ErMao Engine - Web 音频（第一阶段占位：保留完整 AudioSystem API）
//
// 2D 幸存者的逻辑只通过 AudioSystem 播放音效/音乐；Web 第一阶段先静音，
// 后续可用 SDL2 音频或 Web Audio 实现同样的接口，不改玩法代码。
#include "platform/Audio.h"
#include "core/Log.h"

namespace erm {

AudioSystem& AudioSystem::Get() {
    static AudioSystem instance;
    return instance;
}

bool AudioSystem::Init() {
    m_initialized = true;
    m_failed = false;
    LogInfo("[Web] 音频暂未启用（接口已就绪）");
    return true;
}

void AudioSystem::Shutdown() {
    m_initialized = false;
    m_voices.clear();
    m_sounds.clear();
    m_names.clear();
    m_musicName.clear();
}

const AudioSystem::Sound* AudioSystem::Load(const std::string&) { return nullptr; }

void AudioSystem::Play(const std::string&, float, float) {}
void AudioSystem::PlayMusic(const std::string& name, float) { m_musicName = name; }
void AudioSystem::StopMusic() { m_musicName.clear(); }
void AudioSystem::Update(float) {}
void AudioSystem::SetMasterVolume(float volume) { m_masterVolume = volume; }
void AudioSystem::SetMuted(bool muted) { m_muted = muted; }
void AudioSystem::SetMusicEnabled(bool enabled) { m_musicEnabled = enabled; }
void AudioSystem::SetSfxEnabled(bool enabled) { m_sfxEnabled = enabled; }
void AudioSystem::SetMusicVolume(float volume) { m_musicVolume = volume; }
void AudioSystem::SetSfxVolume(float volume) { m_sfxVolume = volume; }

} // namespace erm
