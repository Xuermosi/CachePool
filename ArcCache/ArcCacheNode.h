#pragma once

#include <memory>

namespace XrmsCache
{

template<typename Key, typename Value>
class ArcNode
{
private:
    Key key;
    Value value;
    size_t accessCount_;
    std::shared_ptr<ArcNode> prev_;
    std::shared_ptr<ArcNode> next_;

public:
    ArcNode()
        : acce    
}
}