#pragma once

#include <cmath>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "ICachePolicy.h"
/*在LFU算法之上，引入访问次数平均值概念，
 *当平均值大于最大平均值限制时将所有结点的访问次数减去最大平均值限制的一半或者一个固定值。
 *相当于热点数据“老化”了，这样可以避免频次计数溢出，也可以缓解缓存污染
 */
namespace XrmsCache
{
// 最近最少使用算法
template<typename Key, typename Value>
class LfuCache;

// 用于记录节点访问次数的链表
template<typename Key, typename Value>
/*
 * FreqList类
 * 是一个辅助类，用于管理具有相同访问频率的节点列表。
 */
class FreqList
{
private:
    struct Node
    {
        // 成员变量
        int freq; // 访问频次
        Key key;
        Value value;
        std::shared_ptr<Node> pre;
        std::shared_ptr<Node> next;

        // 无参构造
        Node()
        : freq(1), pre(nullptr), next(nullptr) {}
        // 实参构造
        Node(Key key, Value value)
        : freq(1), key(key), value(value), pre(nullptr), next(nullptr) {}
    };

    using NodePtr = std::shared_ptr<Node>;
    int freq_; // 访问频率
    NodePtr head_; // 哨兵头节点
    NodePtr tail_; // 哨兵尾节点

public:
    explicit FreqList(int n)
        : freq_(n);
    {
        head_ = std::make_shared<Node>();
        tail_ = std::make_shared<Node>();
        head_->next = tail_;
        tail_->prev = head_;
    }

    // 判空
    bool isEmpty() const
    {
        return head->next == tail_;
    }

    // 加入访问链表
    void addNode(NodePtr node)
    {
        if (!node || !head_ || !tail_)
        {
            return;
        }

        // 尾插法
        node->prev = tail_->prev;
        node->next = tail_;
        tail->prev->next = node;
        tail_->prev = node;
    }

// 删除节点
void removeNode(NodePtr node)
{
    if (!node || !head_ || !tail_)
    {
        return;
    }
    // 只有当前节点的时候不删除
    if (!node->pre || !node->next) 
    {
        return;
    }

    // 删除节点
    node->prev->next = node->next;
    node->next->prev = node->prev;
    node->prev = nullptr;
    node->next = nullptr;
}
    // 取链表中的第一个有效节点
    NodePtr getFirstNode() const {return head->next;}

    // 
    friend class LfuCache<Key, Value>;
    //friend class KArcCache<Key, Value>;
};

// 实现了基本的LFU缓存策略
template <typename Key, typename Value>
class LfuCache : public ICachePolicy<Key, Value>
{
public:
    using Node = typename FreqList<Key, Value>::Node;
    using NodePtr = std::shared_ptr<Node>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    // 最大平均值=10
    LfuCache(int capacity, int maxAverageNum = 10)
    : capacity_(capacity), minFreq_(INT8_MAX), maxAverageNum_(maxAverageNum),
      curAverageNum_(0), curTotalNum_(0)
    {}

    ~LfuCache() override = default;

    void put(Key key, Value value) override
    {
        if (capacity_ == 0)
        return;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            // 找到后重置其value值
            it->second->value = value;
            // 找到后直接调整即可，无需去get中再找一遍
            getInternal(it->second, value);
            return;
        }
        
        putInternal(key, value);
    }

    // value值为传出参数
    bool get(Key key, Value value)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if (it != nodeMap_.end())
        {
            getInternal(it->second, value);
            return true;
        }

        return false;
    }

    Value get(Key key) override
    {
        Value value;
        get(key, value);
        return value;
    }

    // 清空缓存，回收资源
    void purge()
    {
        nodeMap_.clear();
        freqToFreqList_.clear();
    }

private:
    void putInternal(Key key, Value value);     // 添加缓存
    void getInternal(Key key, Value value);     // 获取缓存

    void kickOut();     //移除缓存中的过期数据

    void removeFromFreqList(NodePtr node);  // 从频率列表中移除节点
    void addToFreqList(NodePtr node);   // 添加到频率列表
    
    void addFreqNum();    // 增加平均访问等频率
    void decreaseFreqNum(int num);      // 减少平均访问等频率
    void handleOverMaxAverageNum();     // 处理当前平均访问频率超过上限的情况
    void updateMinFreq();

private:
    int     capacity_;      // 缓存容量
    int     minFreq_;       // 最小访问频次 ）(用于找到最小访问频次的节点)
    int     maxAverageNum_; // 最大平均访问频次
    int     curAverageNum_; // 当前平均访问频次
    int     curTotalNum_;   // 当前访问所有缓存次数总数
    std::mutex  mutex_;     // 互斥锁
    NodeMap     nodeMap_;   // key到缓存节点的映射
    std::unordered_map<int, FreqList<Key, Value>*> freqToFreqList_; // 访问频次到该频次链表的映射
};

template<typename Key, typename Value>
void LfuCache<Key, Value>::getInternal(NodePtr node, Value& value)
{
    // 找到之后需要将其从低访问频次的链表中删除，并且添加到+1的访问频次链表中
    // 访问频次+1 并将value值返回
    value = node->value;
    
    // 从原有访问频次链表中删除节点
    removeFromFreqList(node);
    node->freq++; // 节点访问频次+1
    addToFreqList(node);
    /* 如果当前node的访问频次等于minFreq+1, 并且其前驱链表为空，
     * 则说明freqToFreqList_[node->freq - 1]链表中node是其仅有的节点，
     * 此时需要更新最小访问频次
     */
    if (node->freq - 1 == minFreq_ && freqToFreqList_[node->freq - 1]->isEmpty())
        minFreq_++;

    // 总访问频次和当前平均访问频次都随之增加
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::putInternal(Key key, Value value)
{
    // 不在缓存中，需要先判断缓存是否已满
    if (nodeMap_.size() == capacity_)
    {
        // 缓存已满，将最不常访问的节点删除，更新当前平均访问频次和总访问频次
        kickOut();
    }

    // 创建新节点，将新节点添加进入，并更新最小访问频次
    NodePtr node = std::make_shared<Node>(key, value);
    nodeMap_[key] = node;
    addToFreqList(node);
    addFreqNum();
    minFreq_ = std::min(minFreq_, 1);
}

// 删除最不常访问节点并更新当前平均访问频次和总访问频次
template<typename Key, typename Value>
void LfuCache<Key, Value>::kickOut()
{
    // 根据访问频次拿到第一个节点，即最不常访问的节点
    NodePtr node = freqToFreqList_[minFreq_]->getFirstNode();
    // 先从访问频次列表中删除
    removeFromFreqList(node);
    // 再从map中删除
    nodeMap_.erase(node->key);
    // 减少平均访问等频率
    decreaseFreqNum(node->key);
}

template<typename Key, typename Value>
void LfuCache<Key, Value>::removeFromFreqList(NodePtr node)
{
    // 检查节点是否为空
    if (!node)
        return;
    
    // 取出node中的freq访问频次
    auto freq = node->freq;
    // 根据访问频次找到freqList此map中的node并调用node成员方法remove
    freqToFreqList_[freq]->removeNode(node);
}
}