#include "../ICachePolicy.h"
#include "ArcLruPart.h" 
#include "ArcLfuPart.h"
#include <memory>

namespace XrmsCache
{
template<typename Key, typename Value>
class ArcCache : public ICachePolicy<Key, Value>
{
// std::make_unique就是创建并返回一个 std::unique_ptr 智能指针
public:
    // 构造函数 初始化缓存容量和转换阈值 并且构建LRU和LFU部分的缓存实例 
    explicit ArcCache(size_t capacity = 10, size_t transformThreshold = 2)
        : capacity_(capacity)
        , transformThreshold_(transformThreshold)
        , lruPart_(std::make_unique<ArcLruPart<Key, Value>>(capacity, transformThreshold))
        , lfuPart_(std::make_unique<ArcLfuPart<Key, Value>>(capacity, transformThreshold))
    {}

    // 析构函数，使用默认实现
    ~ArcCache() voerride = default;

    // 向缓存中插入键值对
    void put(Key key, Value value) override
    {
        // 检查幽灵缓存中是否存在该键
        bool inGhost = checkGhostCaches(key);

        // 不在幽灵缓存
        if (!inGhost)
        {
            // 如果幽灵缓存中不存在该键，尝试将键值对插入LRU部分
            if (lruPart_->put(key, value))
            {
                // 如果插入LRU部分成功，再将键值对插入LFU部分
                lfuPart_->put(key, value);
            }
        }
        else 
        {
            // 如果幽灵缓存中存在该键，只将键值对插入LRU邠
            lruPart_->put(key, value);
        }
    }

    // 从缓存中获取键对应的值
    // 如果键存在，将值存储在引用我参数value 中，并返回true
    // 如果键不存在，返回false
    bool get(Key key, Value& value) override
    {
        // 检查幽灵缓存中是否存在该键
        checkGhostCaches(key);

        bool shouldTransform = false;
        // 尝试从LRU部分获取键对应的值
        if (lruPart_->get(key, value, shouldTransform))
        {
            // 如果获取成功且满足转换条件
            if (shouldTransform)
            {
                // 将键值对插入LFU部分
                lfuPart_->put(key, value);
            }
            return true;
        }
        // 如果LRU部分找不到，尝试从LFU部分获取
        return lfuPart_->get(key, value);
    }

    // 从缓存中获取键对应的值
    // 调用另一个get方法，并返回获取的值
    Value get(Key key) override
    {
        // 1. 声明一个value类型的变量value并进行默认初始化
        Value value{};
        // 2. 调用另一个重载的get方法，将value作为引用参数传入
        get(key, value);
        // 3. 返回value的值
        return value;
    }

private:
    /* 检查幽灵缓存中是否存在指定的键
     * 如果存在，根据情况调整LRU部分和LFU部分的容量
     * 返回是否能在幽灵缓存中找到该建
     */
    bool checkGhostCaches(Key key)
    {
        bool inGhost = false;
        // 检查LRU部分的幽灵缓存中是否存在该键
        if (lruPart_->checkGhost(key))
        {
            // 如果存在，尝试减少LFU部分的容量
            if (lfuPart_->decreaseCapacity())
            {
                // 如果减少成功，则增加LRU部分的容量
                lruPart_->increaseCapacity();
            }
            inGhost = true;
        }// 如果LRU部分的幽灵缓存中不存在该键，检查LFU部分的幽灵缓存
        else if (lfuPart_->checkGhost(key))
        {
            // 如果存在，尝试减少LRU部分的容量
            if (lruPart_->decreaseCapacity())
            {
                // 如果减少成功，则增加LFU部分的容量
                lfuPart_->increaseCapacity();
            }
            inGhost = true;
        }
        return inGhost;
    }
private:
    // 缓存的总容量
    size_t capacity_;
    // 转换阈值，用于判断是否要将节点从LRU部分转移到LFU部分
    size_t transformThreshold_;

    // 指向LRU部分缓存的智能指针
    std::unique_ptr<ArcLruPart<Key, Value>> lruPart_;
    // 指向LFU部分缓存的智能指针
    std::unique_ptr<ArcLfuPart<Key, Value>> lfuPart_;
};
} // namespace XrmsCache