#include <avengine/vector.h>
#include <iostream>

// Demo for avengine::vector usage
// Run the following command from the repo root
// g++ -std=c++17 -I avengine/include avengine/examples/vector_demo.cpp -o
// /tmp/vector_demo /tmp/vector_demo

int main()
{
  avengine::vector<int> nums;
  nums.pushBack(10);
  nums.pushBack(20);
  nums.pushBack(30);

  std::cout << "size=" << nums.size() << " capacity=" << nums.capacity() << "\n";
  for (size_t i = 0; i < nums.size(); ++i)
  {
    nums.popBack();
    std::cout << "after popBack, back=" << nums.back() << "\n";

    std::cout << "after insert: ";
    for (size_t i = 0; i < nums.size(); ++i)
    {
      std::cout << nums[i] << (i + 1 == nums.size() ? "" : " ");
    }
    std::cout << "\n";

    nums.erase(0);
    std::cout << "after erase: ";
    for (size_t i = 0; i < nums.size(); ++i)
    {
      std::cout << nums[i] << (i + 1 == nums.size() ? "" : " ");
    }
    std::cout << "\n";

    return 0;
  }
