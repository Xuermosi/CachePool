#pragma once

#include <cstring>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "ICachePolicy.h"

// LRU-最近最少使用算法
namespace JazhCache
{
    // 前向声明
template<typename Key, typename Value> class LruCache;

template<typename Key, typename Value>
class LruNode
{
private:
    Key key_;
    Value value_;
    size_t accessCount_;  // 访问次数
    std::shared_ptr<LruNode<Key, Value>> prev_; // 前指针
    std::shared_ptr<LruNode<Key, Value>> next_; // next指针

public:
    // 默认构造函数
    LruNode(Key key, Value value)
        : key_(key)
        , value_(value)
        , accessCount_(1)
        , prev_(nullptr)
        , next_(nullptr)
    {}

    // 提供必要的访问器
    Key getKey() const {return key_; }
    Value getValue() const {return value_; }
    void setValue(const Value& value) {value_ = value; }
    size_t getAccessCount() {++accessCount_; }

    friend class LruCache<Key Value>;
};

// 继承自ICachePolicy接口
template<typename Key, typename Value>
class LruCache : public ICachePolicy<Key, Value>
{
public:
    // 定义类型别名
    using LruNodeType = LruNode<Key, Value>;
    using LruNodePtr = std::shared_ptr<LruNodeType>;
    using LruNodeMap = std::unordered_map<Key, LruNodePtr>;

    LruCache(int capacity)
        : capacity_(capacity)
    {
        initializeList();
    }

    ~LruCache() voerride = default;

    // 添加缓存
    void put(Key key, Value value) override
    {
        if (capacity_ <= 0)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            // 如果在当前容器中，则更新value，并调用get方法，代表该数据刚被访问
            updateExistingNode(it->second, value);
            return ;
        }

        addNewNode(key, value);
    }

    bool get(Key key,Value& value) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            moveToMostRecent(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    Value get(Key key) override
    {
        Value value{};
        // memset(&value, 0, sizeof(value)); // memset 是按字节设置内存的，对于复杂类型（如 string）使用 memset 可能会破坏对象的内部结构
        get(key, value);
        return value;
    }

    // 删除指定元素
    void remove(Key key)
    {
        std::lock_guard<std::mutex> lock<mutex_>;
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            removeNode(it->second);
            nodeMap_.erase(it);
        }
    }

private:
    void initializeList()
    {
        // 创建首尾虚拟节点
        dummyHead_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyTail_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyHead_->next_ = dummyTail_;
        dummyTail_->prev_ = dummyHead_;
    }

    void updateExistingNode(NodePtr node, const Value& value)
    {
        if (nodeMap_.size() >= capacity_)
        {
            evictLeastRecent();
        }

        NodePtr newNode = std::make_shared<LruNodeType>(key, value);
        insertNode(newNode);
        nodeMap_[key] = newNode;
    }

    // 将该节点移动到最新位置
    void moveToMostRecent(NodePtr node)
    {
        removeNode(node);
        insertNode(node);
    }

    void removeNode(NodePtr node)
    {
        node->prev_->next_ = node->next_;
        node->next_->prev_ = node->prev_;
    }

    // 从尾部插入节点
    void insertNode(NodePtr node)
    {
        node->next_ = dummyTail_;
        node->prev_ = dummyTail_->prev_;
        dummyTail_->prev_->next_ = node;
        dummyTail_->prev_ = node;
    }

    // 驱逐最近最少访问
    void evictLeastRecent()
    {
        NodePtr leastRecent = dummyHead_->next_;
        removeNode(leastRecent);
        nodeMap_.erase(leastRecent->getKey());
    }

private:
    int         capacity_;  // 缓存容量
    NodeMap     nodeMap_;   // key->Node
    std::mutex  mutex_;     // 
    NodePtr     dummyHead_; // 虚拟头节点
    NodePtr     dummyTail_; // 哨兵 
};

// LRU优化：Lru-k版本。 通过继承的方式进行再优化
template<typename Key, typename Value>
class LruCache : public LruCache<Key, Value>
{
public:
    LruCache(int capacity, int historyCapacity, int k)
        : LruCache<>
} 
}
