#pragma once

#include "vector.h"

//creates a queue using a circular buffer approach
template <typename T>
class Queue {
public:
    // Default constructor
    // EFFECTS: Constructs an empty queue
    Queue() : data(), front(0), back(0), count(0) {
        //give the default queue a capacity of 8
        for (size_t i = 0; i < 8; ++i){
            data.pushBack(T());
        }
    }

    // REQUIRES: Nothing
    // MODIFIES: *this
    // EFFECTS: Adds x to the back of the queue
    void push(const T& x) {
        if(count == data.capacity()) {
            grow();
        }
        data[back] = x;
        back = increment(back);
        count++;
    }

    // REQUIRES: !empty()
    // MODIFIES: *this
    // EFFECTS: Removes the front element from the queue, returns true on success, false if empty
    bool pop(){
        if(count == 0){
            return false;
        }
        front = increment(front);
        count--;
        return true;
    }

    // REQUIRES: !empty()
    // MODIFIES: Nothing
    // EFFECTS: Returns a reference to the front element of the queue
    T& top() {
        return data[front];
    }

    // REQUIRES: !empty()
    // MODIFIES: Nothing
    // EFFECTS: Returns a const reference to the front element of the queue
    const T& top() const {
        return data[front];
    }

    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns true if the queue is empty, false otherwise
    bool empty() const {
        return count == 0;
    }

    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns the number of elements in the queue
    size_t size() const {
        return count;
    }

    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns true if *this and other contain the same elements
    //          in the same order, false otherwise
    bool operator==(const Queue& other) const {
        if(count != other.count) { return false;}
        
        for(size_t i = 0; i < count; ++i){
            if(data[(front + i) % data.capacity()] != other.data[(other.front + i) % other.data.capacity()]){
                return false;
            }
        }
        return true;
    }

    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns true if *this and other are not equal, false otherwise
    bool operator!=(const Queue& other) const {
        // TODO: Implement
        return !(*this == other);
    }

private:
    vector<T> data;
    size_t front, back, count;

    // REQUIRES: Nothing
    // MODIFIES: Nothing
    // EFFECTS: Returns (inc + 1) % capacity for circular buffer wrapping
    size_t increment(const size_t inc) const {
        if(data.capacity() == 0) {
            return 0;
        }
        return (inc + 1) % data.capacity();
    }

    // REQUIRES: Nothing
    // MODIFIES: *this
    // EFFECTS: Doubles the capacity of the queue and reorganizes elements
    void grow(){
        size_t oldCapacity = data.capacity();
        size_t newCapacity = oldCapacity * 2;
        vector<T> newData = vector(newCapacity, T());
        for(size_t i = 0; i < count; ++i){
            newData[i] = data[(front + i) % oldCapacity];
        }
        data = newData;
        front = 0;
        back = count;
    }
};