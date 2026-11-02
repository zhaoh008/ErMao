// ErMao Engine - 行为树（GAMES104 第 17 讲）
//
// 只做三态（Success/Failure/Running）的最小实现：
//   * 组合节点：Sequence（与）/ Selector（或）
//   * 装饰器：Inverter（取反）/ Cooldown（冷却）/ RepeatUntilFail
//   * 叶子：Condition / Action（std::function，参数是 BTContext）
//
// 设计要点：
//   1. 节点不存"世界"，只通过 BTContext 拿数据；
//   2. Running 表示"这帧没做完"，父节点会停在同一个子节点上继续 tick；
//   3. 树的构建用链式辅助函数，读起来接近伪代码（代码优先）。
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace erm {

enum class BTStatus { Success, Failure, Running };

// 每次 tick 传给叶子的上下文：具体内容由使用方定义（这里放通用 void* + dt）
struct BTContext {
    void* user = nullptr; // 例如 EnemyComponent*
    float dt = 0.0f;
    float time = 0.0f;    // 累计时间（Cooldown 用）
};

class BTNode {
public:
    virtual ~BTNode() = default;
    virtual BTStatus Tick(BTContext& ctx) = 0;
    virtual const char* Name() const = 0;
};

using BTNodePtr = std::unique_ptr<BTNode>;

namespace bt {

// 可变参数构造节点列表：std::initializer_list 的元素是 const，无法移动 unique_ptr
template <typename... Ts>
std::vector<BTNodePtr> NodeList(Ts&&... nodes) {
    std::vector<BTNodePtr> list;
    list.reserve(sizeof...(Ts));
    (list.push_back(std::forward<Ts>(nodes)), ...);
    return list;
}

// 依次执行：任一失败立即失败；全部成功才成功
BTNodePtr Sequence(std::vector<BTNodePtr> children, const char* name = "Sequence");
// 依次尝试：任一成功立即成功；全部失败才失败
BTNodePtr Selector(std::vector<BTNodePtr> children, const char* name = "Selector");

BTNodePtr Condition(std::function<bool(BTContext&)> fn, const char* name = "Condition");
BTNodePtr Action(std::function<BTStatus(BTContext&)> fn, const char* name = "Action");
// 只执行一帧并总是返回 Success 的动作（适合"设置黑板"）
BTNodePtr SetBool(bool* flag, bool value, const char* name = "SetBool");

BTNodePtr Inverter(BTNodePtr child, const char* name = "Inverter");
// 冷却：每隔 interval 秒才允许子树 tick，其余时间返回 Failure
BTNodePtr Cooldown(BTNodePtr child, float intervalSeconds, const char* name = "Cooldown");
// 子树成功则继续重复，失败则返回 Success（"一直做直到失败"）
BTNodePtr RepeatUntilFail(BTNodePtr child, const char* name = "RepeatUntilFail");

} // namespace bt

// 行为树：持有根节点 + 累计时间
class BehaviorTree {
public:
    void SetRoot(BTNodePtr root) { m_root = std::move(root); }
    bool IsValid() const { return m_root != nullptr; }

    BTStatus Tick(BTContext& ctx) {
        m_time += ctx.dt;
        ctx.time = m_time;
        return m_root ? m_root->Tick(ctx) : BTStatus::Failure;
    }

private:
    BTNodePtr m_root;
    float     m_time = 0.0f;
};

} // namespace erm
