// ErMao Engine - 编辑器撤销/重做历史实现
#include "editor/UndoHistory.h"

#include "core/Json.h"
#include "core/Log.h"
#include "scene/World.h"

#include <algorithm>

namespace erm {

namespace {
constexpr size_t kMinHistoryLimit = 2;
} // namespace

void UndoHistory::Reset() {
    m_states.clear();
    m_index = 0;
}

void UndoHistory::SetLimit(size_t limit) {
    m_limit = (std::max)(limit, kMinHistoryLimit);
    // 优先淘汰最旧的状态；如果当前状态就在最前面，只能丢弃后面的 redo 分支。
    while (m_states.size() > m_limit) {
        if (m_index > 0) {
            m_states.erase(m_states.begin());
            --m_index;
        } else {
            m_states.pop_back();
        }
    }
}

bool UndoHistory::Capture(const World& world) {
    const std::string text = world.ToJson().Dump();
    if (text.empty()) return false;

    if (m_states.empty()) {
        m_states.push_back(text);
        m_index = 0;
        return true;
    }
    if (text == m_states[m_index]) return false;

    // 在历史中间做新编辑：旧的 redo 分支作废。
    if (m_index + 1 < m_states.size()) {
        m_states.erase(m_states.begin() + static_cast<std::ptrdiff_t>(m_index + 1), m_states.end());
    }
    m_states.push_back(text);
    m_index = m_states.size() - 1;

    while (m_states.size() > m_limit) {
        m_states.erase(m_states.begin());
        if (m_index > 0) --m_index;
    }
    return true;
}

void UndoHistory::SyncCurrent(const World& world) {
    if (m_states.empty()) return;
    const std::string text = world.ToJson().Dump();
    if (!text.empty()) m_states[m_index] = text;
}

bool UndoHistory::Undo(World& world) {
    if (!CanUndo()) return false;
    return Apply(world, m_index - 1);
}

bool UndoHistory::Redo(World& world) {
    if (!CanRedo()) return false;
    return Apply(world, m_index + 1);
}

bool UndoHistory::Apply(World& world, size_t index) {
    if (index >= m_states.size()) return false;

    const std::string text = m_states[index];
    std::string       error;
    JsonValue         root = JsonValue::Parse(text, &error);
    if (!error.empty() || !root.IsObject()) {
        LogError("[撤销] 历史快照解析失败: %s", error.c_str());
        return false;
    }
    if (!world.FromJson(root, false)) {
        LogError("[撤销] 恢复场景失败");
        return false;
    }
    // FromJson 可能因四元数归一化产生 1 ULP 漂移。把条目文本更新成实际状态，
    // 否则下一帧 Capture 会把这次恢复误认成一次新编辑，导致 redo 分支被清掉。
    const std::string actual = world.ToJson().Dump();
    if (!actual.empty()) m_states[index] = actual;
    m_index = index;
    return true;
}

} // namespace erm
