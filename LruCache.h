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
    // 前向声明，为了在类定义之前引用这个类
template<typename Key, typename Value> class LruCache;

template<typename Key, typename Value>
class LruNode
{
private:
    Key key_;
    Value value_;
    size_t accessCount_;  // 访问次数
    std::shared_ptr<LruNode<Key, Value>> prev_; // prev指针
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

// 让LruCache继承自ICachePolicy接口  重写里面两个get和一个put方法
template<typename Key, typename Value>
class LruCache : public ICachePolicy<Key, Value>
{
public:
    // 定义类型别名
    using LruNodeType = LruNode<Key, Value>;
    using LruNodePtr = std::shared_ptr<LruNodeType>;  // 使用智能指针
    using LruNodeMap = std::unordered_map<Key, LruNodePtr>;

    // 根据容量参数构造
    LruCache(int capacity)
        : capacity_(capacity)
    {
        initializeList();
    }

    ~LruCache() override = default;

    // key存在则更新，不存在则向缓存中插入key-value
    void put(Key key, Value value) override
    {
        // 先确定容量够不够
        if (capacity_ <= 0)
        {
            return;
        }

        // 互斥锁
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

    // 利用key尝试取缓存中的页，返回true或false
    bool get(Key key,Value& value) override  // override明确地指示一个函数是基类虚函数的重写
    {
        // 上锁
        std::lock_guard<std::mutex> lock(mutex_);
        // 查找当前key在不在缓存中
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end()) // 说明找到了
        {
            // 此节点现在变成最新访问的 需要将其置于最新位置
            moveToMostRecent(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    // 根据key取value
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
    // 链表的初始化函数
    void initializeList()
    {
        // 创建首尾虚拟节点，作为哨兵节点
        dummyHead_ = std::make_shared<LruNodeType>(Key(), Value());
        dummyTail_ = std::make_shared<LruNodeType>(Key(), Value());
        // 一开始的链表只包含头尾哨兵
        dummyHead_->next_ = dummyTail_;
        dummyTail_->prev_ = dummyHead_;
    }

    // 尝试命中节点
    void updateExistingNode(NodePtr node, const Value& value)
    {
        // 确定当前哈希表是否已经超过链表容量
        if (nodeMap_.size() >= capacity_)
        {
            // 超过的话就将最近最少使用的页面调出内存
            evictLeastRecent();
        }
        // 没超过就把节点加入
        NodePtr newNode = std::make_shared<LruNodeType>(key, value);
        insertNode(newNode);
        nodeMap_[key] = newNode;
    }

    // 将该节点移动到最新位置
    // 最新使用的节点要置于尾部
    void moveToMostRecent(NodePtr node)
    {
        removeNode(node);
        insertNode(node);
    }

    // 从链表中删除当前节点
    void removeNode(NodePtr node)
    {
        node->prev_->next_ = node->next_;
        node->next_->prev_ = node->prev_;
        // 没有释放是为了后续操作
    }

    // 最新的节点要置于链表尾部，所以这里采用的尾插法
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
        // 从哈希表中删除
        nodeMap_.erase(leastRecent->getKey());
    }

private:
    int         capacity_;  // 缓存容量
    NodeMap     nodeMap_;   // key->Node
    std::mutex  mutex_;     // 
    NodePtr     dummyHead_; // 头节点哨兵
    NodePtr     dummyTail_; // 尾节点哨兵 
};

// LRU优化：Lru-k版本。 通过继承的方式进行再优化
template<typename Key, typename Value>
class LruCache : public LruCache<Key, Value>
{
public:
    LruCache(int capacity, int historyCapacity, int k)
        : LruCache<Key, Value>(capacity)  // 调用基类构造
        , historyList_(std::make_unique<LruCache<Key, size_t>>)
} 
}
