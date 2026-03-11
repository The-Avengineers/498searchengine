#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <avengine/avengine_lib/PriorityQueue.h>
#include <avengine/avengine_lib/vector.h>

using namespace avengine;

// Note on comparator semantics: the heap swaps parent with child when
// comp(parent, child) is true, so larger-wins with less<T> means the
// default priorityQueue<T> is a MAX-heap (greatest element at top).
// Using greater<T> gives a MIN-heap.

// ---------------------------------------------------------------------------
// empty / size
// ---------------------------------------------------------------------------

TEST_CASE("priorityQueue - default construction yields empty queue")
{
  priorityQueue<int> pq;
  CHECK(pq.empty() == true);
  CHECK(pq.size() == 0);
}

TEST_CASE("priorityQueue - size tracks push operations")
{
  priorityQueue<int> pq;
  pq.push(1);
  CHECK(pq.size() == 1);
  pq.push(2);
  CHECK(pq.size() == 2);
  pq.push(3);
  CHECK(pq.size() == 3);
}

TEST_CASE("priorityQueue - size decreases on pop")
{
  priorityQueue<int> pq;
  pq.push(5);
  pq.push(3);
  pq.pop();
  CHECK(pq.size() == 1);
  pq.pop();
  CHECK(pq.size() == 0);
  CHECK(pq.empty() == true);
}

// ---------------------------------------------------------------------------
// MAX-heap behavior (default less<T>)
// ---------------------------------------------------------------------------

TEST_CASE("priorityQueue - less<T> gives max-heap: top is largest element")
{
  priorityQueue<int> pq;
  pq.push(5);
  pq.push(3);
  pq.push(7);
  pq.push(1);
  CHECK(pq.top() == 7);
}

TEST_CASE("priorityQueue - less<T> pop removes elements in descending order")
{
  priorityQueue<int> pq;
  pq.push(5);
  pq.push(3);
  pq.push(7);
  pq.push(1);

  CHECK(pq.top() == 7);
  pq.pop();
  CHECK(pq.top() == 5);
  pq.pop();
  CHECK(pq.top() == 3);
  pq.pop();
  CHECK(pq.top() == 1);
}

// ---------------------------------------------------------------------------
// MIN-heap behavior (greater<T>)
// ---------------------------------------------------------------------------

TEST_CASE("priorityQueue - greater<T> gives min-heap: top is smallest element")
{
  priorityQueue<int, greater<int>> pq;
  pq.push(5);
  pq.push(3);
  pq.push(7);
  pq.push(1);
  CHECK(pq.top() == 1);
}

TEST_CASE("priorityQueue - greater<T> pop removes elements in ascending order")
{
  priorityQueue<int, greater<int>> pq;
  pq.push(5);
  pq.push(3);
  pq.push(7);
  pq.push(1);

  CHECK(pq.top() == 1);
  pq.pop();
  CHECK(pq.top() == 3);
  pq.pop();
  CHECK(pq.top() == 5);
  pq.pop();
  CHECK(pq.top() == 7);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST_CASE("priorityQueue - pop on single element empties the queue")
{
  priorityQueue<int> pq;
  pq.push(42);
  pq.pop();
  CHECK(pq.empty() == true);
}

TEST_CASE("priorityQueue - pop on empty queue does not crash")
{
  priorityQueue<int> pq;
  pq.pop();
  CHECK(pq.empty() == true);
}

// ---------------------------------------------------------------------------
// Constructor from vector
// ---------------------------------------------------------------------------

TEST_CASE("priorityQueue - vector constructor builds valid max-heap")
{
  vector<int> v;
  v.pushBack(5);
  v.pushBack(3);
  v.pushBack(7);
  v.pushBack(1);
  v.pushBack(9);
  v.pushBack(2);

  priorityQueue<int, less<int>> pq(v, less<int>());
  // Max-heap: pops in descending order 9, 7, 5, ...
  CHECK(pq.top() == 9);
  pq.pop();
  CHECK(pq.top() == 7);
  pq.pop();
  CHECK(pq.top() == 5);
}

TEST_CASE("priorityQueue - vector constructor builds valid min-heap with greater<T>")
{
  vector<int> v;
  v.pushBack(5);
  v.pushBack(3);
  v.pushBack(7);
  v.pushBack(1);

  priorityQueue<int, greater<int>> pq(v, greater<int>());
  CHECK(pq.top() == 1);
  pq.pop();
  CHECK(pq.top() == 3);
}

// ---------------------------------------------------------------------------
// clear
// ---------------------------------------------------------------------------

TEST_CASE("priorityQueue - clear empties the queue")
{
  priorityQueue<int> pq;
  pq.push(1);
  pq.push(2);
  pq.push(3);
  pq.clear();
  CHECK(pq.empty() == true);
  CHECK(pq.size() == 0);
}

TEST_CASE("priorityQueue - push after clear works correctly")
{
  priorityQueue<int> pq;
  pq.push(10);
  pq.push(20);
  pq.clear();
  pq.push(5);
  CHECK(pq.size() == 1);
  CHECK(pq.top() == 5);
}

// ---------------------------------------------------------------------------
// Copy constructor
// ---------------------------------------------------------------------------

TEST_CASE("priorityQueue - copy constructor produces independent copy")
{
  priorityQueue<int> pq1;
  pq1.push(5);
  pq1.push(3);
  pq1.push(7);

  priorityQueue<int> pq2(pq1);
  CHECK(pq2.size() == 3);
  CHECK(pq2.top() == 7);

  // Modifying copy does not affect original
  pq2.pop();
  CHECK(pq1.size() == 3);
  CHECK(pq1.top() == 7);
}

// ---------------------------------------------------------------------------
// serializeHeap
// ---------------------------------------------------------------------------

TEST_CASE("priorityQueue - serializeHeap first element matches top")
{
  priorityQueue<int> pq;
  pq.push(5);
  pq.push(3);
  pq.push(7);
  const auto& heap = pq.serializeHeap();
  CHECK(heap[0] == pq.top());
}

TEST_CASE("priorityQueue - serializeHeap size matches queue size")
{
  priorityQueue<int> pq;
  pq.push(1);
  pq.push(2);
  pq.push(3);
  CHECK(pq.serializeHeap().size() == pq.size());
}

// ---------------------------------------------------------------------------
// Heap property under mixed operations
// ---------------------------------------------------------------------------

TEST_CASE("priorityQueue - heap property holds after interleaved push and pop")
{
  priorityQueue<int> pq; // max-heap
  pq.push(10);
  pq.push(4);
  pq.push(8);
  pq.pop(); // removes 10, top becomes 8
  pq.push(2);
  pq.push(6);
  pq.pop(); // removes 8, top becomes 6

  // Drain and verify each extracted value <= previous (descending order)
  int prev = pq.top();
  pq.pop();
  while (!pq.empty())
  {
    CHECK(pq.top() <= prev);
    prev = pq.top();
    pq.pop();
  }
}
