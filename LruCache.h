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

// LRU-k算法是对LRU算法的改进，基础的LRU算法被访问数据进入缓存队列只需要访问(put、get)一次就行，
// 但是现在需要被访问k（大小自定义）次才能被放入缓存中，
// 基础的LRU算法可以看成是LRU-1。
// LRU-k算法有两个队列一个是缓存队列，一个是数据访问历史队列。
// 当访问一个数据时，首先将其添加进入访问历史队列并进行累加访问次数，
// 当该数据的访问次数超过k次后，才将数据缓存到缓存队列，从而避免缓存队列被冷数据所污染。
// 同时访问历史队列中的数据也不是一直保留的，也是需要按照LRU的规则进行淘汰的。

// LRU优化：Lru-k版本。 通过继承的方式进行再优化
template<typename Key, typename Value>
class LruCache : public LruCache<Key, Value>
{
public:
    LruCache(int capacity, int historyCapacity, int k)
        : LruCache<Key, Value>(capacity)  // 调用基类构造
        , historyList_(std::make_unique<LruCache<Key, size_t>>)
        , k_(k)
        {}

    // LRU-k算法的get方法
    Value get(Key key)
    {
        // 从访问历史队列中获取该数据的访问次数
        int historyCount = historyList_->get(key);
        // 如果访问到数据，更新历史访问记录节点值count++
        historyList_->put(key, historyCount + 1);

        // 从缓存中获取数据，不一定能获取到，因为可能不在缓存中
        return LruCache<Key, Value>::get(key);
    }

    void put(Key key, Value value)
    {
        // 先判断是否存在于缓存中，如果存在则直接覆盖，不存在的话不能直接添加到缓存中
        if (LruCache<Key, Value>::get(key) != " ")
            LruCache<Key, Value>::put(key, value);
        
        // 只有数据的历史访问次数达到上限才添加到缓存中
        int historyCount = historyList_->get(key);
        historyList_->put(key, historyCount + 1);
        // 如果历史访问次数达到上限，就将数据添加到缓存中
        if (historyCount >= k_)
        {
            // 先移除历史访问记录
            historyList_->remove(key);
            // 再添加到缓存中
            LruCache<Key, Value>::put(key, value);
        }
    }

private:
    int k_;   // 进入缓存队列的访问次数上限 一般置为2
    std::unique_ptr<LruCache<Key, size_t>> historyList_; // 访问历史队列
};


/*
 * 有一定开发经验的同学一定发现了，该项目中锁的粒度很大，
 * 并且这个锁的粒度还不好直接减少。毕竟涉及线程安全的地方实在是太多了，
 * 甚至连使用读写锁都没办法优化。那我们只好换个思路，将lru分片。
 * 根据传入的key值进行哈希运算后找到对应的lru分片，然后调用该分片相应的方法。
 * 大家试想一下当多个线程同时访问一个LRU时（LFU同理），
 * 由于锁的粒度大，会造成长时间的同步等待，但如果是多个线程同时访问多个LRU缓存，
 * 那么同步等待时间大大减少了。  
 */

// LRU优化：对LRU进行分片，可以提高高并发使用的性能
template<typename Key, typename Value>
class HashLruCaches
{
public:
    HashCaches(size_t capacity, int sliceNum)
        : capacity_(capacity)
          // 若传入值为0，则初始化为当前系统的硬件并发线程数，通过hardware_concurrecy获取
        , sliceNum_(sliceNum > 0? sliceNum : std::thread::hardware_concurrency())
    {
        // 获取每个切片的大小
        size_t sliceSize = std::ceil(capacity / static_cast<double>(sliceNum_));
        for (int i = 0; i < sliceNum_; ++i)
        {
            // 加强版pushback 也就是在把obj加入vec之前才new这个obj
            lruSliceCaches_.emplace_back(new LruCache<Key, Value>(sliceSize));
        }
    }

    // HashLru中的put方法
    void put(Key key, Value value)
    {
        // 获取key的hash值， 并计算出对应的分片索引
        size_t sliceIndex = Hash(key) % sliceNum_;
        // 再调用该切片上的lru块的put方法
        return lruSliceCaches_[sliceIndex]->put(key, value);
    }

    // HashLru中的get方法
    bool get(Key key, Value& value)
    {
        // 获取key的hash值，并计算出对应的分片索引
        size_t sliceIndex = Hash(Key) % sliceNum_;
        return lruSliceCaches_[sliceIndex]->get(key, value);
    }

    Value get(Key key, Value& value)
    {
        Value value;
        // 将 value 对象的内存内容置为0
        memset(&value, 0, sizeof(value));
        get(key, value);
        return value;
    }

private:
    // 将key转换为对应的hash值
    size_t(Key key)
    {
        std::hash<Key> hashFunc;
        return hashFunc(key);
    }

private:
    size_t  capacity_; // 总容量
    int     sliceNum_; //切片数量
    // 这里声明了一个LruCache类型的智能指针数组。HashLruCaches将多个LruCache对象组合在一起，形成一个整体。
    // 因此这里两个类是组合关系，HashCaches依赖于LruCache
    std::vector<std::unique_ptr<LruCache<Key, Value>>> lruSliceCaches_; // 切片lru缓存
};
}  // namespace JazhCache