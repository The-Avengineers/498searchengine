#include "vector.h"
#include <cstddef>
#include <functional>

namespace avengine
{

template <typename T> struct less
{
  bool operator()(const T& a, const T& b) const
  {
    return a < b;
  }
};

template <typename T> struct greater
{
  bool operator()(const T& a, const T& b) const
  {
    return a > b;
  }
};

template <typename T, typename Compare = less<T>> class priorityQueue
{
public:
  priorityQueue() : data(), comp() {}

  ~priorityQueue() {}

  priorityQueue(const priorityQueue& other) : data(other.data), comp(other.comp) {}

  // constructor that takes in a vector (seed list) and builds pq
  priorityQueue(const vector<T>& vec, const Compare& comp_in) : data(vec), comp(comp_in)
  {
    if (!data.empty())
    {
      for (size_t i = data.size() / 2; i > 0; --i)
      {
        heapify_down(i - 1);
      }
    }
  }

  priorityQueue& operator=(const priorityQueue& other)
  {
    if (this != &other)
    {
      data = other.data;
      comp = other.comp;
    }
    return *this;
  }

  size_t size() const
  {
    return data.size();
  }

  bool empty() const
  {
    return data.empty();
  }

  const T& top() const
  {
    if (!data.empty())
    {
      return data[0];
    }
  }

  void push(const T& val)
  {
    data.pushBack(val);
    heapify_up(data.size() - 1);
  }

  void pop()
  {
    if (empty())
      return;
    data[0] = data[data.size() - 1];
    data.popBack();
    if (!empty())
    {
      heapify_down(0);
    }
  }

  void clear()
  {
    data.clear();
  }

  const vector<T>& serializeHeap() const
  {
    return data;
  }

private:
  vector<T> data;
  Compare comp;

  void heapify_up(size_t index)
  {
    while (index > 0)
    {
      size_t parent_idx = parent(index);
      if (comp(data[parent_idx], data[index]))
      {
        T temp = data[parent_idx];
        data[parent_idx] = data[index];
        data[index] = temp;
        index = parent_idx;
      }
      else
      {
        break;
      }
    }
  }

  void heapify_down(size_t index)
  {
    size_t heap_size = data.size();
    while (true)
    {
      size_t left = left_child(index);
      size_t right = right_child(index);
      size_t largest = index;

      if (left < heap_size && comp(data[largest], data[left]))
      {
        largest = left;
      }
      if (right < heap_size && comp(data[largest], data[right]))
      {
        largest = right;
      }

      if (largest != index)
      {
        T temp = data[index];
        data[index] = data[largest];
        data[largest] = temp;
        index = largest;
      }
      else
      {
        break;
      }
    }
  }

  static size_t parent(size_t index)
  {
    return (index - 1) / 2;
  }

  static size_t left_child(size_t index)
  {
    return 2 * index + 1;
  }

  static size_t right_child(size_t index)
  {
    return 2 * index + 2;
  }
};

} // namespace avengine