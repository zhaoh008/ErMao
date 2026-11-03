// ErMao Engine - 编辑器撤销/重做历史
//
// 快照式实现：每个条目保存一份 World JSON 快照。编辑器里的检视器、Gizmo、
// 创建/删除、光照参数等操作最终都改 World，序列化后天然覆盖全部字段，
// 不需要为每种操作写一份逆操作。代价是每条记录都存一份场景文本，因此
// 限制历史条数（默认 64；场景较大时也只占几 MB）。
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace erm {

class World;

class UndoHistory {
public:
    // 清空历史；下一次 Capture 记录初始状态。
    void Reset();

    // 把当前 World 记成一条历史。与当前条目相同则返回 false。
    // 每次新编辑都会丢弃当前位置之后的 redo 分支。
    bool Capture(const World& world);

    // 外部已把 World 恢复成当前条目的状态（例如 Play-In-Editor Stop）后，
    // 把条目文本同步成实际序列化结果，避免 1 ULP 的四元数漂移被误判成
    // 一次新编辑而清空 redo。
    void SyncCurrent(const World& world);

    bool CanUndo() const { return m_index > 0; }
    bool CanRedo() const { return !m_states.empty() && m_index + 1 < m_states.size(); }

    // 恢复上一条 / 下一条；没有可撤销项或 JSON 解析失败时返回 false。
    bool Undo(World& world);
    bool Redo(World& world);

    size_t Size() const { return m_states.size(); }
    size_t Index() const { return m_index; }
    // 上限至少为 2（一条当前状态 + 一条可撤销状态）。
    void   SetLimit(size_t limit);
    size_t Limit() const { return m_limit; }

private:
    bool Apply(World& world, size_t index);

    std::vector<std::string> m_states;
    size_t                   m_index = 0; // 当前状态在 m_states 中的下标
    size_t                   m_limit = 64;
};

} // namespace erm
