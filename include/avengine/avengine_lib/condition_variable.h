#pragma once

#include "mutex.h"
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
