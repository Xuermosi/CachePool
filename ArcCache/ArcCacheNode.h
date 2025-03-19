#pragma once

#include <memory>

namespace XrmsCache
{

template<typename Key, typename Value>
class ArcNode
{
private:
    Key key_;
    Value value_;
    size_t accessCount_; // 访问次数（用于实现LFU策略）
    std::shared_ptr<ArcNode> prev_;
    std::shared_ptr<ArcNode> next_;

public:
    ArcNode()
        : accessCount_(1), prev_(nullptr), next_(nullptr) {}

    ArcNode(Key key, Value value)
        : key_(key)
        , value_(value)
        , accessCount_(1)
        , prev_(nullptr)
        , next_(nullptr)   
    {}

    // ======================
    // 访问器（Getters）
    // ======================

    /** @brief 获取节点键 */
    Key getKey() const { return key_; }

    /** @brief 获取节点值 */
    Value getValue() const { return value_; }

    /** @brief 获取访问计数 （用于LFU策略） */
    size_t getAccessCount() const { return accessCount_; }

    // ======================
    // 修改器（Setters）
    // ======================

    /** @brief 设置节点值（更新操作） */
    void setValue(const Value& value) { value_ = value; }

    /** @brief 增加访问计数（每次访问时调用，实现LFU） */
    void incrementAccessCount() { ++accessCount_; }

    // ======================
    // 友元声明（链表操作需要）
    // ======================

    /**
     * @brief 友元类声明
     * ArcLruPart：ARC算法的LRU部分
     * ArcLfuPart：ARC算法的LFU部分
     * 允许这两个部分访问私有成员（链表操作需要修改prev/next指针）
     */
    template<typename K, typename V> friend class ArcLruPart;
    template<typename K, typename V> friend class ArcLfuPart;
};

} // namespace XrmsCache