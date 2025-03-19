#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <iomanip>
#include <random>
#include <algorithm>

// 引入自定义的缓存策略头文件
#include "ICachePolicy.h"
#include "LfuCache.h"
#include "LruCache.h"
#include "ArcCache/ArcCache.h"


// 定时器类，用于记录代码执行时间
class Timer {
public:
    // 构造函数，在创建对象时记录当前时间
    Timer() : start_(std::chrono::high_resolution_clock::now()) {}// 拥有最小计次周期的时钟

    // 计算从对象创建到调用该函数时所经过的时间(以毫秒为单位)
    double elapsed() {
        auto now = std::chrono::high_resolution_clock::now();
        /* std::chrono::milliseconds 是 std::chrono 库中预定义的一种时长类型，表示毫秒
         * std::chrono::duration_cast 是一个模板函数，
         * 用于将一个 std::chrono::duration 对象从一种时间单位转换为另一种时间单位。
         */
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
    }

private:
    // 记录对象创建时的时间点
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

// 辅助函数：打印不同缓存策略的命中率结果
void printResults (const std::string& testName, int capacity,
                   const std::vector<int>& get_operations,
                   const std::vector<int>& hits) 
    {
    std::cout << "缓存大小: " << capacity << std::endl;
    // 输出LRU缓存策略的命中率，保留两位小数
    std::cout << "LRU - 命中率: " << std::fixed << std::setprecision(2)
              << (100.0 * hits[0] / get_operations[0]) << "%" << std::endl;
    // 输出LFU缓存策略的命中率，保留两位小数
    std::cout << "LRU - 命中率: " << std::fixed << std::setprecision(2)
              << (100.0 * hits[1] / get_operations[1]) << "%" << std::endl;
              // 输出LFU缓存策略的命中率，保留两位小数
    std::cout << "ARC - 命中率: " << std::fixed << std::setprecision(2)
              << (100.0 * hits[2] / get_operations[2]) << "%" << std::endl;
    }


// 测试场景1：热点数据访问测试
void testHotDataAccess() 
{
    std::cout << "\n=== 测试场景1：热点数据访问测试 ===" << std::endl;

    // 定义缓存的容量
    const int CAPACITY = 50;
    // 定义操作的总次数
    const int OPERATIONS = 500000;
    // 定义热点数据的数量
    const int HOT_KEYS = 20;
    // 定义冷数据的数量
    const int COLD_KEYS = 5000;

    // 创建不同缓存策略的对象，容量均为CAPACITY
    XrmsCache::LruCache<int, std::string> lru(CAPACITY);
    XrmsCache::LfuCache<int, std::string> lfu(CAPACITY);
    XrmsCache::ArcCache<int, std::string> arc(CAPACITY);

    // 随机数生成器的种子
    std::random_device rd;
    // 随机数生成器
    std::mt19937 gen(rd());

    // 存储不同缓存策略对象的指针数组
    std::array<XrmsCache::ICachePolicy<int, std::string>*, 3> caches = {&lru, &lfu, &arc};
    // 存储不同缓存策略的命中次数，初始化为0
    std::vector<int> hits(3,0);
    // 存储不同缓存策略的get操作次数，初始化为0
    std::vector<int> get_operations(3,0);

    // 对每个缓存策略进行操作
    for (int i = 0; i < caches.size(); ++i)
    {
        // 进行一系列put操作
        for (int op = 0; op < OPERATIONS; ++op) {
            int key;
            if (op % 100 < 70) // 70%的概率访问热点数据
            {
                key = gen() % HOT_KEYS;
            }
            else 
            {
                key = HOT_KEYS + (gen() % COLD_KEYS);
            }
            // 生成对应的值
            std::string value = "value" + std::to_string(key);
            // 将键值对放入缓存
            caches[i]->put(key, value);
        }

        // 进行随机get操作
        for (int get_op = 0; get_op < OPERATIONS; ++ get_op)
        {
            int key;
            if (get_op % 100 < 70) // 70%的概率访问热点数据
            {
                key = gen() % HOT_KEYS;
            }
            else // 30%的概率访问冷数据 
            {
                // key=HOT_KEYS + ..是为了保证冷数据和热点数据的键无交集，保证两种数据的独立性
                key = HOT_KEYS + (gen() % COLD_KEYS);
            }

            std::string result;
            // 增加get操作次数
            get_operations[i]++;
            // 尝试从缓存中获取键对应的值
            if (caches[i]->get(key, result))
            {
                // 如果获取成功，增加命中次数
                hits[i]++;
            }
        }
    }

    // 打印测试结果
    printResults("热点数据访问测试", CAPACITY, get_operations, hits);
}


// 测试场景2：循环扫描测试
void testLoopPattern()
{
    std::cout << "\n=== 测试场景2：循环扫描测试 ===" << std::endl;

    // 定义缓存的容量
    const int CAPACITY = 50;
    // 定义循环的大小
    const int LOOP_SIZE = 500;
    // 定义操作的总次数
    const int OPERATIONS = 200000;
    XrmsCache::LruCache<int, std::string> lru(CAPACITY);
    XrmsCache::LfuCache<int, std::string> lfu(CAPACITY);
    XrmsCache::ArcCache<int, std::string> arc(CAPACITY);
    // 创建不同缓存策略对象的指针数组
    std::array<XrmsCache::ICachePolicy<int, std::string>*, 3> caches = {&lru, &lfu, &arc};
    // 存储不同缓存策略的命中次数，初始化为0
    std::vector<int> hits(3, 0);
    // 存储不同缓存策略的get操作次数，初始化为0
    std::vector<int> get_operations(3, 0);

    // 随机数生成器的种子
    std::random_device rd;
    // 随机数生成器
    std::mt19937 gen(rd());

    // 对每个缓存策略进行操作
    for (int i = 0; i < caches.size(); ++i)
    {
        // 填充数据到缓存
        for (int key = 0; key < LOOP_SIZE; ++key)
        {
            std::string value = "loop" + std::to_string(key);
            caches[i]->put(key, value);
        }

        // 进行访问测试
        int current_pos = 0;
        for (int op = 0; op < OPERATIONS; ++op)
        {
            int key;
            if (op % 100 < 60) { // 60%的概率进行顺序扫描
                key = current_pos;
                current_pos = (current_pos + 1) % LOOP_SIZE;
            } else if (op % 100 < 90) { // 30%的概率进行随机扫描
                key = gen() % LOOP_SIZE;
            } else {    // 10%的概率访问范围外的数据
                key = LOOP_SIZE + (gen() % LOOP_SIZE);
            }

            std::string result;
            // 增加get操作次数
            get_operations[i]++;
            // 尝试从缓存中获取键对应的值
            if (caches[i]->get(key, result))
            {
                // 如果获取成功，增加命中数
                hits[i]++;
            }
        }
    }
    // 打印测试结果
    printResults("循环扫描测试", CAPACITY, get_operations, hits);
}

// 测试场景3：工作负载剧烈变化测试
void testWorkloadShift()
{
    std::cout << "\n=== 测试场景3：工作负载剧烈变化测试 ===" << std::endl;

     // 定义缓存的容量
     const int CAPACITY = 4;
     // 定义操作的总次数
     const int OPERATIONS = 80000;
     // 定义每个阶段的操作次数
     const int PHASE_LENGTH = OPERATIONS / 5;

     // 创建不同缓存策略的对象，容量均为CAPACITY
     XrmsCache::LruCache<int, std::string> lru(CAPACITY);
     XrmsCache::LfuCache<int, std::string> lfu(CAPACITY);
     XrmsCache::ArcCache<int, std::string> arc(CAPACITY);

     // 随机数生成器的种子
     std::random_device rd;
     // 随机数生成器
     std::mt19937 gen(rd());
     // 存储不同缓存策略对象的指针数组
     std::array<XrmsCache::ICachePolicy<int, std::string>*, 3> caches = {&lru, &lfu, &arc};
     // 存储不同缓存策略对象的命中次数，初始化为0
     std::vector<int> hits(3,0);
     // 存储不同缓存策略对象的get操作次数，初始化为0
     std::vector<int> get_operations(3,0);

     // 对每个缓存策略进行操作
     for (int i = 0; i < caches.size(); ++i)
     {
        // 填充一些初始数据到缓存
        for (int key = 0; key < 1000; ++key)
        {
            std::string value = "init" + std::to_string(key);
            caches[i]->put(key, value);
        }

        // 进行多阶段测试
        for (int op = 0; op < OPERATIONS; ++op)
        {
            int key;
            // 根据不同阶段选择不同的访问模式
            if (op < PHASE_LENGTH) // 热点访问
            {
            // 生成0-4之间的随机数，缓存的访问主要集中在少数几个热点键上
                key = gen() % 5;
                } 

            else if (op < PHASE_LENGTH * 2) // 大范围随机
            {
            //生成 0 到 999 之间的随机数，访问范围相对较广，没有明显的热点数据
            // 缓存的访问是随机且分散的，没有明显的规律。
                key = gen() % 1000;
            }
            else if (op < PHASE_LENGTH * 3) // 顺序扫描
            {
                // 按照顺序依次访问 0 到 99 之间的键，模拟了顺序读取数据的场景
                key = (op - PHASE_LENGTH * 2) % 100;
            }
            else if (op < PHASE_LENGTH * 4)// 局部性随机
            {
                int locality = (op / 1000) % 10; // 分为十个组
                key = locality * 20 + (gen() % 20);  // 每个组的起始键
            }
            else // 混合访问
            {
                int r = gen() % 10;
                if (r < 30)
                {
                    key = gen() % 5;
                }
                else if (r < 60)
                {
                    key = 5 + (gen() % 95);
                }
                else
                {
                    key = 100 + (gen() % 900);
                }
            }

            std::string result;
            get_operations[i]++;
            if (caches[i]->get(key, result))
            {
                hits[i]++;
            }

            // 随机进行put操作，更新缓存内容
            if (gen() % 100 < 30)
            {
                std::string value = "new" + std::to_string(key);
                caches[i]->put(key, value);
            }
        }
    }
    printResults("工作负载剧烈变化测试", CAPACITY, get_operations, hits);
}

int main()
{
    testHotDataAccess();
    testLoopPattern();
    testWorkloadShift();
    return 0;
}
    