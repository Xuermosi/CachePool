#pragma once

#include "ArcCacheNode.h"
#include <unordered_map>
#include <map>
#include <mutex>

namespace XrmsCache 
{

// 定义一个模板类 ArcLfuPart，用于实现 ARC（Adaptive Replacement Cache）缓存算法中的 LFU（Least Frequently Used）部分
// Key 是缓存键的类型，Value 是缓存值的类型
template<typename Key, typename Value>
class ArcLfuPart 
{
public:
    // 定义类型别名，方便后续使用
    using NodeType = ArcNode<Key, Value>;  // 节点类型，使用 ArcNode 模板类
    using NodePtr = std::shared_ptr<NodeType>;  // 节点指针类型，使用智能指针管理节点
    using NodeMap = std::unordered_map<Key, NodePtr>;  // 主缓存和幽灵缓存使用的映射类型，键为 Key，值为节点指针
    using FreqMap = std::map<size_t, std::list<NodePtr>>;  // 频率映射类型，键为访问频率，值为节点指针列表

    // 构造函数，接受缓存容量和转换阈值作为参数
    // capacity 表示主缓存的容量
    // transformThreshold 表示从 LFU 部分转换到其他部分的阈值
    explicit ArcLfuPart(size_t capacity, size_t transformThreshold)
        : capacity_(capacity)  // 初始化主缓存容量
        , ghostCapacity_(capacity)  // 初始化幽灵缓存容量，与主缓存容量相同
        , transformThreshold_(transformThreshold)  // 初始化转换阈值
        , minFreq_(0)  // 初始化最小访问频率为 0
    {
        initializeLists();  // 调用初始化函数，初始化幽灵缓存的链表
    }

    // 向缓存中插入一个键值对
    // key 是要插入的键
    // value 是要插入的值
    // 返回插入是否成功
    bool put(Key key, Value value) 
    {
        if (capacity_ == 0) 
            return false;  // 如果缓存容量为 0，插入失败

        std::lock_guard<std::mutex> lock(mutex_);  // 加锁，保证线程安全
        auto it = mainCache_.find(key);  // 在主缓存中查找键
        if (it != mainCache_.end()) 
        {
            return updateExistingNode(it->second, value);  // 如果键已存在，更新节点的值和频率
        }
        return addNewNode(key, value);  // 如果键不存在，添加新节点
    }

    // 从缓存中获取指定键的值
    // key 是要获取的键
    // value 用于存储获取到的值
    // 返回是否成功获取到值
    bool get(Key key, Value& value) 
    {
        std::lock_guard<std::mutex> lock(mutex_);  // 加锁，保证线程安全
        auto it = mainCache_.find(key);  // 在主缓存中查找键
        if (it != mainCache_.end()) 
        {
            updateNodeFrequency(it->second);  // 如果键存在，更新节点的访问频率
            value = it->second->getValue();  // 获取节点的值
            return true;  // 返回获取成功
        }
        return false;  // 如果键不存在，返回获取失败
    }

    // 检查幽灵缓存中是否存在指定键
    // key 是要检查的键
    // 返回幽灵缓存中是否存在该键
    bool checkGhost(Key key) 
    {
        auto it = ghostCache_.find(key);  // 在幽灵缓存中查找键
        if (it != ghostCache_.end()) 
        {
            removeFromGhost(it->second);  // 如果键存在，从幽灵缓存中移除该节点
            ghostCache_.erase(it);  // 从幽灵缓存映射中删除该键值对
            return true;  // 返回存在该键
        }
        return false;  // 如果键不存在，返回不存在该键
    }

    // 增加主缓存的容量
    void increaseCapacity() { ++capacity_; }
    
    // 减少主缓存的容量
    // 返回减少容量是否成功
    bool decreaseCapacity() 
    {
        if (capacity_ <= 0) return false;  // 如果容量已经为 0，减少失败
        if (mainCache_.size() == capacity_) 
        {
            evictLeastFrequent();  // 如果主缓存已满，移除最不常使用的节点
        }
        --capacity_;  // 减少容量
        return true;  // 返回减少成功
    }

private:
    // 初始化幽灵缓存的链表
    void initializeLists() 
    {
        ghostHead_ = std::make_shared<NodeType>();  // 创建幽灵缓存链表的头节点
        ghostTail_ = std::make_shared<NodeType>();  // 创建幽灵缓存链表的尾节点
        ghostHead_->next_ = ghostTail_;  // 头节点的下一个节点指向尾节点
        ghostTail_->prev_ = ghostHead_;  // 尾节点的前一个节点指向头节点
    }

    // 更新已存在节点的值和频率
    // node 是要更新的节点指针
    // value 是要更新的值
    // 返回更新是否成功
    bool updateExistingNode(NodePtr node, const Value& value) 
    {
        node->setValue(value);  // 更新节点的值
        updateNodeFrequency(node);  // 更新节点的访问频率
        return true;  // 返回更新成功
    }

    // 向主缓存中添加新节点
    // key 是新节点的键
    // value 是新节点的值
    // 返回添加是否成功
    bool addNewNode(const Key& key, const Value& value) 
    {
        if (mainCache_.size() >= capacity_) 
        {
            evictLeastFrequent();  // 如果主缓存已满，移除最不常使用的节点
        }

        NodePtr newNode = std::make_shared<NodeType>(key, value);  // 创建新节点
        mainCache_[key] = newNode;  // 将新节点添加到主缓存映射中
        
        // 将新节点添加到频率为 1 的列表中
        if (freqMap_.find(1) == freqMap_.end()) 
        {
            freqMap_[1] = std::list<NodePtr>();  // 如果频率为 1 的列表不存在，创建一个新的列表
        }
        freqMap_[1].push_back(newNode);  // 将新节点添加到频率为 1 的列表末尾
        minFreq_ = 1;  // 更新最小访问频率为 1
        
        return true;  // 返回添加成功
    }

    // 更新节点的访问频率
    // node 是要更新频率的节点指针
    void updateNodeFrequency(NodePtr node) 
    {
        size_t oldFreq = node->getAccessCount();  // 获取节点的旧访问频率
        node->incrementAccessCount();  // 增加节点的访问频率
        size_t newFreq = node->getAccessCount();  // 获取节点的新访问频率

        // 从旧频率列表中移除节点
        auto& oldList = freqMap_[oldFreq];
        oldList.remove(node);
        if (oldList.empty()) 
        {
            freqMap_.erase(oldFreq);  // 如果旧频率列表为空，从频率映射中删除该频率项
            if (oldFreq == minFreq_) 
            {
                minFreq_ = newFreq;  // 如果旧频率是最小频率，更新最小频率为新频率
            }
        }

        // 添加到新频率列表
        if (freqMap_.find(newFreq) == freqMap_.end()) 
        {
            freqMap_[newFreq] = std::list<NodePtr>();  // 如果新频率列表不存在，创建一个新的列表
        }
        freqMap_[newFreq].push_back(node);  // 将节点添加到新频率列表末尾
    }

    // 移除最不常使用的节点
    void evictLeastFrequent() 
    {
        if (freqMap_.empty()) 
            return;  // 如果频率映射为空，直接返回

        // 获取最小频率的列表
        auto& minFreqList = freqMap_[minFreq_];
        if (minFreqList.empty()) 
            return;  // 如果最小频率列表为空，直接返回

        // 移除最少使用的节点
        NodePtr leastNode = minFreqList.front();
        minFreqList.pop_front();

        // 如果该频率的列表为空，则删除该频率项
        if (minFreqList.empty()) 
        {
            freqMap_.erase(minFreq_);
            // 更新最小频率
            if (!freqMap_.empty()) 
            {
                minFreq_ = freqMap_.begin()->first;
            }
        }

        // 将节点移到幽灵缓存
        if (ghostCache_.size() >= ghostCapacity_) 
        {
            removeOldestGhost();  // 如果幽灵缓存已满，移除最旧的幽灵节点
        }
        addToGhost(leastNode);  // 将节点添加到幽灵缓存
        
        // 从主缓存中移除
        mainCache_.erase(leastNode->getKey());
    }

    // 从幽灵缓存链表中移除指定节点
    // node 是要移除的节点指针
    void removeFromGhost(NodePtr node) 
    {
        node->prev_->next_ = node->next_;  // 修改前一个节点的下一个指针
        node->next_->prev_ = node->prev_;  // 修改后一个节点的前一个指针
    }

    // 将节点添加到幽灵缓存链表中
    // node 是要添加的节点指针
    void addToGhost(NodePtr node) 
    {
        node->next_ = ghostTail_;  // 节点的下一个节点指向尾节点
        node->prev_ = ghostTail_->prev_;  // 节点的前一个节点指向尾节点的前一个节点
        ghostTail_->prev_->next_ = node;  // 尾节点的前一个节点的下一个节点指向该节点
        ghostTail_->prev_ = node;  // 尾节点的前一个节点指向该节点
        ghostCache_[node->getKey()] = node;  // 将节点添加到幽灵缓存映射中
    }

    // 移除幽灵缓存中最旧的节点
    void removeOldestGhost() 
    {
        NodePtr oldestGhost = ghostHead_->next_;  // 获取最旧的幽灵节点
        if (oldestGhost != ghostTail_) 
        {
            removeFromGhost(oldestGhost);  // 从幽灵缓存链表中移除该节点
            ghostCache_.erase(oldestGhost->getKey());  // 从幽灵缓存映射中删除该键值对
        }
    }

private:
    size_t capacity_;  // 主缓存的容量
    size_t ghostCapacity_;  // 幽灵缓存的容量
    size_t transformThreshold_;  // 从 LFU 部分转换到其他部分的阈值
    size_t minFreq_;  // 最小访问频率
    std::mutex mutex_;  // 互斥锁，用于保证线程安全

    NodeMap mainCache_;  // 主缓存映射，存储键值对
    NodeMap ghostCache_;  // 幽灵缓存映射，存储被移除的节点
    FreqMap freqMap_;  // 频率映射，存储每个访问频率对应的节点列表
    
    NodePtr ghostHead_;  // 幽灵缓存链表的头节点
    NodePtr ghostTail_;  // 幽灵缓存链表的尾节点
};

} // namespace XrmsCache