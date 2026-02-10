#pragma once
#include "../avengine_lib/string.h"
#include "../avengine_lib/deque.h"

namespace avengine {

struct urlEntry {
    string url;
    string domain;
    float priority;     
    int depth;   // from seed
};

struct hostQueue {
    string domain;          
    deque<urlEntry> pending;   // URLs waiting to be fetched from this domain
};

struct heapEntry {
    string domain;
    float priority;

    bool operator<(const heapEntry& other) const {
        return priority < other.priority;
    }
};

}