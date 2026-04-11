#include <avengine/avengine_lib/atomic.h>
#include <avengine/avengine_lib/thread.h>

#include <cstdio>
#include <cstdlib>

namespace
{

void fail(const char* expr, int line)
{
  std::fprintf(stderr, "atomic_test failed at line %d: %s\n", line, expr);
  std::abort();
}

#define EXPECT(expr)                                                                               \
  do                                                                                               \
  {                                                                                                \
    if (!(expr))                                                                                   \
      fail(#expr, __LINE__);                                                                       \
  } while (false)

// basic store/load on single thread

void testStoreLoad()
{
  avengine::atomic_counter c(0);
  EXPECT(c.load() == false);
  EXPECT(c.get_val() == 0);

  c.store(1);
  EXPECT(c.load() == true);
  EXPECT(c.get_val() == 1);

  c.store(0);
  EXPECT(c.load() == false);
}

// fetch_add on single thread
void testFetchAdd()
{
  avengine::atomic_counter c(0);
  int prev = c.fetch_add(1);
  EXPECT(prev == 0);
  EXPECT(c.get_val() == 1);

  prev = c.fetch_add(4);
  EXPECT(prev == 1);
  EXPECT(c.get_val() == 5);
}

// concurrent fetch_add from multiple thread

static const int kThreads = 8;
static const int kIncrementsPerThread = 10000;

struct IncrArgs
{
  avengine::atomic_counter* counter;
  int increments;
};

void* incrementWorker(void* raw)
{
  IncrArgs* args = static_cast<IncrArgs*>(raw);
  for (int i = 0; i < args->increments; ++i)
    args->counter->fetch_add(1);
  return nullptr;
}

void testConcurrentFetchAdd()
{
  avengine::atomic_counter counter(0);
  avengine::thread workers[kThreads];
  IncrArgs args[kThreads];

  for (int i = 0; i < kThreads; ++i)
  {
    args[i] = {&counter, kIncrementsPerThread};
    workers[i].start(incrementWorker, &args[i]);
  }
  for (int i = 0; i < kThreads; ++i)
    workers[i].join();

  EXPECT(counter.get_val() == kThreads * kIncrementsPerThread);
}

// store from one thread is visible in another

struct StoreArgs
{
  avengine::atomic_counter* flag;
  avengine::atomic_counter* result;
};

void* storeWorker(void* raw)
{
  StoreArgs* args = static_cast<StoreArgs*>(raw);
  args->flag->store(1);
  return nullptr;
}

void* loadWorker(void* raw)
{
  StoreArgs* args = static_cast<StoreArgs*>(raw);

  while (args->flag->get_val() == 0)
    ;
  args->result->store(1);
  return nullptr;
}

void testCrossThreadVisibility()
{
  avengine::atomic_counter flag(0);
  avengine::atomic_counter result(0);
  StoreArgs args = {&flag, &result};

  avengine::thread loader, storer;
  loader.start(loadWorker, &args);
  storer.start(storeWorker, &args);

  storer.join();
  loader.join();

  EXPECT(result.get_val() == 1);
}

} // namespace

int main()
{
  testStoreLoad();
  testFetchAdd();
  testConcurrentFetchAdd();
  testCrossThreadVisibility();
  std::printf("atomic_test: all tests passed\n");
  return 0;
}
