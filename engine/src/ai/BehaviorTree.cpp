// ErMao Engine - 行为树实现（GAMES104 第 17 讲）
#include "ai/BehaviorTree.h"

namespace erm {
namespace bt {

namespace {

// 组合节点基类：记住"上次执行到哪个子节点"，支持 Running 续跑
class CompositeNode : public BTNode {
public:
    CompositeNode(std::vector<BTNodePtr> children, const char* name)
        : m_children(std::move(children)), m_name(name) {}

    const char* Name() const override { return m_name; }

protected:
    std::vector<BTNodePtr> m_children;
    size_t                 m_runningChild = 0;
    const char*            m_name = "Composite";
};

class SequenceNode : public CompositeNode {
public:
    SequenceNode(std::vector<BTNodePtr> children, const char* name)
        : CompositeNode(std::move(children), name) {}

    BTStatus Tick(BTContext& ctx) override {
        for (size_t i = m_runningChild; i < m_children.size(); ++i) {
            const BTStatus status = m_children[i]->Tick(ctx);
            if (status == BTStatus::Running) {
                m_runningChild = i; // 下次从同一个子节点继续
                return BTStatus::Running;
            }
            if (status == BTStatus::Failure) {
                m_runningChild = 0;
                return BTStatus::Failure;
            }
        }
        m_runningChild = 0;
        return BTStatus::Success;
    }
};

class SelectorNode : public CompositeNode {
public:
    SelectorNode(std::vector<BTNodePtr> children, const char* name)
        : CompositeNode(std::move(children), name) {}

    BTStatus Tick(BTContext& ctx) override {
        for (size_t i = m_runningChild; i < m_children.size(); ++i) {
            const BTStatus status = m_children[i]->Tick(ctx);
            if (status == BTStatus::Running) {
                m_runningChild = i;
                return BTStatus::Running;
            }
            if (status == BTStatus::Success) {
                m_runningChild = 0;
                return BTStatus::Success;
            }
        }
        m_runningChild = 0;
        return BTStatus::Failure;
    }
};

class ConditionNode : public BTNode {
public:
    ConditionNode(std::function<bool(BTContext&)> fn, const char* name) : m_fn(std::move(fn)), m_name(name) {}
    BTStatus Tick(BTContext& ctx) override { return m_fn(ctx) ? BTStatus::Success : BTStatus::Failure; }
    const char* Name() const override { return m_name; }

private:
    std::function<bool(BTContext&)> m_fn;
    const char*                     m_name;
};

class ActionNode : public BTNode {
public:
    ActionNode(std::function<BTStatus(BTContext&)> fn, const char* name) : m_fn(std::move(fn)), m_name(name) {}
    BTStatus Tick(BTContext& ctx) override { return m_fn(ctx); }
    const char* Name() const override { return m_name; }

private:
    std::function<BTStatus(BTContext&)> m_fn;
    const char*                         m_name;
};

class SetBoolNode : public BTNode {
public:
    SetBoolNode(bool* flag, bool value, const char* name) : m_flag(flag), m_value(value), m_name(name) {}
    BTStatus Tick(BTContext&) override {
        *m_flag = m_value;
        return BTStatus::Success;
    }
    const char* Name() const override { return m_name; }

private:
    bool*       m_flag;
    bool        m_value;
    const char* m_name;
};

class InverterNode : public BTNode {
public:
    InverterNode(BTNodePtr child, const char* name) : m_child(std::move(child)), m_name(name) {}
    BTStatus Tick(BTContext& ctx) override {
        const BTStatus status = m_child->Tick(ctx);
        if (status == BTStatus::Success) return BTStatus::Failure;
        if (status == BTStatus::Failure) return BTStatus::Success;
        return status;
    }
    const char* Name() const override { return m_name; }

private:
    BTNodePtr   m_child;
    const char* m_name;
};

class CooldownNode : public BTNode {
public:
    CooldownNode(BTNodePtr child, float interval, const char* name)
        : m_child(std::move(child)), m_interval(interval), m_name(name) {}

    BTStatus Tick(BTContext& ctx) override {
        if (ctx.time < m_nextAllowed) return BTStatus::Failure;
        const BTStatus status = m_child->Tick(ctx);
        if (status != BTStatus::Running) m_nextAllowed = ctx.time + m_interval;
        return status;
    }
    const char* Name() const override { return m_name; }

private:
    BTNodePtr   m_child;
    float       m_interval = 1.0f;
    float       m_nextAllowed = 0.0f;
    const char* m_name;
};

class RepeatUntilFailNode : public BTNode {
public:
    RepeatUntilFailNode(BTNodePtr child, const char* name) : m_child(std::move(child)), m_name(name) {}
    BTStatus Tick(BTContext& ctx) override {
        const BTStatus status = m_child->Tick(ctx);
        if (status == BTStatus::Failure) return BTStatus::Success;
        return BTStatus::Running; // 没失败就继续（下一帧还会 tick 同一个子树）
    }
    const char* Name() const override { return m_name; }

private:
    BTNodePtr   m_child;
    const char* m_name;
};

} // namespace

BTNodePtr Sequence(std::vector<BTNodePtr> children, const char* name) {
    return std::make_unique<SequenceNode>(std::move(children), name);
}
BTNodePtr Selector(std::vector<BTNodePtr> children, const char* name) {
    return std::make_unique<SelectorNode>(std::move(children), name);
}
BTNodePtr Condition(std::function<bool(BTContext&)> fn, const char* name) {
    return std::make_unique<ConditionNode>(std::move(fn), name);
}
BTNodePtr Action(std::function<BTStatus(BTContext&)> fn, const char* name) {
    return std::make_unique<ActionNode>(std::move(fn), name);
}
BTNodePtr SetBool(bool* flag, bool value, const char* name) {
    return std::make_unique<SetBoolNode>(flag, value, name);
}
BTNodePtr Inverter(BTNodePtr child, const char* name) { return std::make_unique<InverterNode>(std::move(child), name); }
BTNodePtr Cooldown(BTNodePtr child, float intervalSeconds, const char* name) {
    return std::make_unique<CooldownNode>(std::move(child), intervalSeconds, name);
}
BTNodePtr RepeatUntilFail(BTNodePtr child, const char* name) {
    return std::make_unique<RepeatUntilFailNode>(std::move(child), name);
}

} // namespace bt
} // namespace erm
