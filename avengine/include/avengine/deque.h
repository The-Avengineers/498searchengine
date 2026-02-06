#pragma once

#include <avengine/vector.h>

namespace avengine
{

// creates a Deque using a circular buffer approach
template <typename T> class Deque
{
public:
  // Default constructor
  // EFFECTS: Constructs an empty Deque
  Deque() : data(), fr(0), bck(0), count(0)
  {
    // give the default Deque a capacity of 8
    for (size_t i = 0; i < 8; ++i)
    {
      data.pushBack(T());
    }
  }

  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Adds x to the front of the deque
  void pushFront(const T& x)
  {
    if (count == data.capacity())
    {
      grow();
    }
    fr = decrement(fr);
    data[fr] = x;
    count++;
  }

  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Adds x to the back of the Deque
  void pushBack(const T& x)
  {
    if (count == data.capacity())
    {
      grow();
    }
    data[bck] = x;
    bck = increment(bck);
    count++;
  }

  // REQUIRES: !empty()
  // MODIFIES: *this
  // EFFECTS: Removes the front element from the Deque, returns true on success, false if empty
  bool popFront()
  {
    if (count == 0)
    {
      return false;
    }
    fr = increment(fr);
    count--;
    return true;
  }

  // REQUIRES: !empty()
  // MODIFIES: *this
  // EFFECTS: Removes the back element from the Deque, returns true on success, false if empty
  bool popBack()
  {
    if (count == 0)
    {
      return false;
    }
    bck = decrement(bck);
    count--;
    return true;
  }
  // REQUIRES: !empty()
  // MODIFIES: Nothing
  // EFFECTS: Returns a reference to the front element of the Deque
  T& front()
  {
    return data[fr];
  }

  // REQUIRES: !empty()
  // MODIFIES: Nothing
  // EFFECTS: Returns a const reference to the front element of the Deque
  const T& front() const
  {
    return data[fr];
  }

  // REQUIRES: !empty()
  // MODIFIES: Nothing
  // EFFECTS: Returns a reference to the back element of the Deque
  T& back()
  {
    if (bck == 0)
    {
      return data[data.capacity() - 1];
    }

    return data[bck - 1];
  }

  // REQUIRES: !empty()
  // MODIFIES: Nothing
  // EFFECTS: Returns a const reference to the back element of the Deque
  const T& back() const
  {
    if (bck == 0)
    {
      return data[data.capacity() - 1];
    }

    return data[bck - 1];
  }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true if the Deque is empty, false otherwise
  bool empty() const
  {
    return count == 0;
  }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns the number of elements in the Deque
  size_t size() const
  {
    return count;
  }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true if *this and other contain the same elements
  //          in the same order, false otherwise
  bool operator==(const Deque& other) const
  {
    if (count != other.count)
    {
      return false;
    }

    for (size_t i = 0; i < count; ++i)
    {
      if (data[(fr + i) % data.capacity()] != other.data[(other.fr + i) % other.data.capacity()])
      {
        return false;
      }
    }
    return true;
  }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns true if *this and other are not equal, false otherwise
  bool operator!=(const Deque& other) const
  {
    return !(*this == other);
  }

private:
  vector<T> data;
  size_t fr, bck, count;

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns (inc + 1) % capacity for circular buffer wrapping
  size_t increment(const size_t inc) const
  {
    if (data.capacity() == 0)
    {
      return 0;
    }
    return (inc + 1) % data.capacity();
  }

  // REQUIRES: Nothing
  // MODIFIES: Nothing
  // EFFECTS: Returns (inc - 1) % capacity for circular buffer wrapping
  size_t decrement(const size_t dec) const
  {
    if (data.capacity() == 0)
    {
      return 0;
    }

    return (dec == 0) ? data.capacity() - 1 : (dec - 1);
  }

  // REQUIRES: Nothing
  // MODIFIES: *this
  // EFFECTS: Doubles the capacity of the Deque and reorganizes elements
  void grow()
  {
    size_t oldCapacity = data.capacity();
    size_t newCapacity = oldCapacity * 2;
    vector<T> newData = vector(newCapacity, T());
    for (size_t i = 0; i < count; ++i)
    {
      newData[i] = data[(fr + i) % oldCapacity];
    }
    data = newData;
    fr = 0;
    bck = count;
  }
};
} // namespace avengine
