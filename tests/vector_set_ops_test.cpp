#include "avengine/avengine_lib/vector.h"

#include <cassert>

namespace
{
using avengine::vector;
}

int main()
{
  vector<int> a{1, 2, 4, 5};
  vector<int> b{2, 3, 5, 6};

  vector<int> intersection = a.setIntersection(b);
  assert(intersection.size() == 2);
  assert(intersection[0] == 2);
  assert(intersection[1] == 5);

  vector<int> joined = a.setUnion(b);
  assert(joined.size() == 6);
  assert(joined[0] == 1);
  assert(joined[1] == 2);
  assert(joined[2] == 3);
  assert(joined[3] == 4);
  assert(joined[4] == 5);
  assert(joined[5] == 6);

  vector<int> diff = a.setDifference(b);
  assert(diff.size() == 2);
  assert(diff[0] == 1);
  assert(diff[1] == 4);

  vector<int> dupA{1, 2, 2, 4};
  vector<int> dupB{2, 2, 3, 4};
  vector<int> dupIntersection = dupA.setIntersection(dupB);
  assert(dupIntersection.size() == 2);
  assert(dupIntersection[0] == 2);
  assert(dupIntersection[1] == 4);

  vector<int> dupUnion = dupA.setUnion(dupB);
  assert(dupUnion.size() == 4);
  assert(dupUnion[0] == 1);
  assert(dupUnion[1] == 2);
  assert(dupUnion[2] == 3);
  assert(dupUnion[3] == 4);

  vector<int> dupDiffLeft{1, 1, 2, 4, 4};
  vector<int> dupDiffRight{2, 3};
  vector<int> dupDiff = dupDiffLeft.setDifference(dupDiffRight);
  assert(dupDiff.size() == 2);
  assert(dupDiff[0] == 1);
  assert(dupDiff[1] == 4);

  vector<int> unsorted{5, 1, 4, 2, 3};
  unsorted.sort();
  assert(unsorted.size() == 5);
  assert(unsorted[0] == 1);
  assert(unsorted[1] == 2);
  assert(unsorted[2] == 3);
  assert(unsorted[3] == 4);
  assert(unsorted[4] == 5);

  vector<int> descending{1, 4, 2, 5, 3};
  descending.sort([](int lhs, int rhs) { return lhs > rhs; });
  assert(descending[0] == 5);
  assert(descending[1] == 4);
  assert(descending[2] == 3);
  assert(descending[3] == 2);
  assert(descending[4] == 1);

  return 0;
}
