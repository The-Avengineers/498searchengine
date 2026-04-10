#pragma once

#include "mutex.h"
#include <cstddef>
#include <pthread.h>
#include <time.h>

namespace avengine
{

class condition_variable
{

public:
  condition_variable()
  {
    pthread_cond_init(&cv, nullptr);
  }

  ~condition_variable()
  {
    pthread_cond_destroy(&cv);
  }

  condition_variable(const condition_variable&) = delete;

  condition_variable& operator=(const condition_variable&) = delete;

  void wait(mutex& m)
  {
    pthread_cond_wait(&cv, m.mutex_var());
  }

  // Waits for at most timeoutMs milliseconds. Returns true if the condition
  // variable was signaled, false on timeout or error.
  bool waitFor(mutex& m, size_t timeoutMs)
  {
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);

    ts.tv_sec += static_cast<time_t>(timeoutMs / 1000u);
    ts.tv_nsec += static_cast<long>((timeoutMs % 1000u) * 1000000u);
    if (ts.tv_nsec >= 1000000000L)
    {
      ts.tv_sec += 1;
      ts.tv_nsec -= 1000000000L;
    }

    const int rc = pthread_cond_timedwait(&cv, m.mutex_var(), &ts);
    return rc == 0;
  }

  void signal()
  {
    pthread_cond_signal(&cv);
  }

  void broadcast()
  {
    pthread_cond_broadcast(&cv);
  }

private:
  pthread_cond_t cv;
};

} // namespace avengine
