#pragma once

#include "ArcCacheNode.h"
#include <unordered_map>
#include <mutex>

namespace XrmsCache
{

template<typename Key, typename Value>
class ArcLruPart
{
public:
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    // 构造函数 初始化缓存容量和转换阈值，并初始化链表
    explicit ArcLruPart(size_t capacity, size_t transformThreshold)
        : capacity_(capacity)
        , ghostCapacity_(capacity)
        , transformThreshold_(transformThreshold)
    {
        initializeLists();
    }


    // 向缓存中插入键值对
    bool put (Key key, Value value)
    {
        if (capacity_ == 0) return false;

        // 使用互斥锁保护对缓存的并发访问
        std::lock_guard<std::mutex> lock(mutex_);
        // 在主缓存中查找键
        auto it = mainCache_.find(key);
        // 存在
        if (it != mainCache_.end())
        {
            return updateExistingNode(it->second, value);
        }
        return addNewNode(key, value);
    }

    bool get(Key key, Value& value, bool& shouldTransform)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if (it != mainCache_.end())
        {
            shouldTransform = updateNodeAccess(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }
    // 检查幽灵缓存中是否存在键
    bool checkGhost (Key key)
    {
        auto it = ghostCache_.find(key);
        if (it != ghostCache_.end())
        {
            removeFromGhost(it->second);
            ghostCache_.erase(it);
            return true;
        }
        return false;
    }

    // 增加缓存容量
    void increaseCapacity() { ++capacity_; }

    // 减少缓存容量
    bool decreaseCapacity()
    {
        if (capacity_ <= 0) return false;
        if (mainCache_.size() == capacity_)
        {
            // 如果当前缓存已经满了 驱逐最近最少使用节点
            evictLeastRecent();
        }
        -- capacity_;
        return true;
    }

private:
    // 初始化主链表和幽灵链表的头节点和尾节点
    void initializeLists()
    {
        mainHead_ = std::make_shared<NodeType>();
        mainTail_ = std::make_shared<NodeType>();
        mainHead_->next_ = mainTail_;
        mainTail_->prev_ = mainHead_;

        ghostHead_ = std::make_shared<NodeType>();
        ghostTail_ = std::make_shared<NodeType>();
        ghostHead_->next_ = ghostTail_;
        ghostTail_->prev_ = ghostHead_;
    }

    //  将节点移动到链表头部，以此表明它是最近被使用的
    bool updateExistingNode(NodePtr node, const Value& value)
    {
        node->setValue(value);
        moveToFront(node);
        return true;
    }

    // 添加新节点到主缓存
    bool addNewNode(const Key& key, const Value& value)
    {
        if (mainCache_.size() >= capacity_)
        {
            // 主缓存满了 驱逐最近最少使用节点
            evictLeastRecent();
        }

        NodePtr newNode = std::make_shared<NodeType>(key, value);
        mainCache_[key] = newNode;
        // 将新节点添加到主链表的头部
        addToFront(newNode);
        return true;
    }

    // 更新节点的访问次数，将其移动到主链表的头部
    bool updateNodeAccess(NodePtr node)
    {
        moveToFront(node);
        node->incrementAccessCount();
        return node->getAccessCount() >= transformThreshold_; // 返回是否超过转换阈值
    }

    // 将节点从当前位置移除，并移动到主链表的头部
    void moveToFront(NodePtr node)
    {
        // 先从当前位置移除
        node->prev_->next_ = node->next_;
        node->next_->prev_ = node->prev_;

        // 添加到头部
        addToFront(node);
    }

    // 节点插到链表头部
    void addToFront(NodePtr node)
    {
        node->next_ = mainHead_->next_;
        node->prev_ = mainHead_;
        mainHead_->next_->prev_ = node;
        mainHead_->next_ = node;  
    }

    // 驱逐主链表中最近最少使用的节点
    // 并移动到幽灵链表中
    void evictLeastRecent()
    {
        // 最近最少使用的节点在主链表的末尾
        NodePtr leastRecent = mainTail_->prev_;
        if (leastRecent == mainHead_) // 主链表为空
            return;
        
        // 从主链表中移除
        removeFromMain(leastRecent);

        // 添加到幽灵缓存
        if (ghostCache_.size() >= ghostCapacity_) // 判断幽灵缓存是否已满
        {
            removeOldestGhost(); // 移除最近最少使用节点
        }
        addToGhost(leastRecent);

        // 从主缓存映射中移除
        mainCache_.erase(leastRecent->getKey());
    }

    // 从主链表中移除节点
    void removeFromMain(NodePtr node)
    {
        node->prev_->next_ = node->next_;
        node->next_->prev_ = node->prev_;
    }

    // 从幽灵链表中移除节点
    void removeFromGhost(NodePtr node)
    {
        node->prev_->next_ = node->next_;
        node->next_->prev_ = node->prev_;
    }

    // 将节点添加到幽灵链表的头部并更新幽灵缓存映射
    void addToGhost(NodePtr node)
    {
        // 重置节点的访问次数
        node->accessCount_ = 1;
        
        // 添加到幽灵缓存的头部
        node->next_ = ghostHead_->next_;
        node->prev_ = ghostHead_;
        ghostHead_->next_->prev_ = node;
        ghostHead_->next_ = node;

        // 添加到幽灵缓存映射
        ghostCache_[node->getKey()] = node;
    }

    // 移除幽灵链表中最旧的节点 也就是最末的节点
    void removeOldestGhost()
    {
        NodePtr oldestGhost = ghostTail_->prev_;
        if (oldestGhost == ghostHead_)
            return; // 幽灵链表为空
        
        removeFromGhost(oldestGhost);
        ghostCache_.erase(oldestGhost->getKey());
    }

private:
    size_t capacity_;           // 主缓存的容量
    size_t ghostCapacity_;      // 幽灵缓存的容量
    size_t transformThreshold_; // 转换门槛值
    std::mutex mutex_;          // 互斥锁

    NodeMap mainCache_;     // 主缓存映射，用于快速查找主缓存中的节点
    NodeMap ghostCache_;    // 幽灵缓存映射，用于快速查找幽灵缓存中的节点

    // 主链表的头尾节点 
    NodePtr mainHead_;
    NodePtr mainTail_;

    // 幽灵链表的头尾节点
    NodePtr ghostHead_;
    NodePtr ghostTail_;
};

}   // namespace XrmsCache