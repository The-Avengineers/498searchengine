#pragma once

#include <pthread.h>

namespace avengine
{

class mutex
{

public:

  mutex()
  {
    pthread_mutex_init(&mu, nullptr);
  }

  ~mutex()
  {
    pthread_mutex_destroy(&mu);
  }

  mutex(const mutex&) = delete;

  mutex& operator=(const mutex&) = delete;

  void lock()
  {
    pthread_mutex_lock(&mu);
  }

  bool try_lock()
  {
    return pthread_mutex_trylock(&mu) == 0;
  }

  void unlock()
  {
    pthread_mutex_unlock(&mu);
  }

  pthread_mutex_t* mutex_var()
  {
    return &mu;
  }

private:

  pthread_mutex_t mu;

};

} // namespace avengine
