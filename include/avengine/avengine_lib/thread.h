#pragma once

#include <pthread.h>

namespace avengine
{

class thread
{
public:
  typedef void* (*entry_fn)(void*);

  thread() : started_(false) {}

  thread(entry_fn fn, void* arg) : started_(false)
  {
    start(fn, arg);
  }

  ~thread()
  {
    if (started_)
    {
      pthread_detach(thread_);
    }
  }

  thread(const thread&) = delete;

  thread& operator=(const thread&) = delete;

  bool start(entry_fn fn, void* arg)
  {
    if (started_)
    {
      return false;
    }

    if (fn == nullptr)
    {
      return false;
    }

    if (pthread_create(&thread_, nullptr, fn, arg) != 0)
    {
      return false;
    }

    started_ = true;
    return true;
  }

  bool join(void** returnValue = nullptr)
  {
    if (!started_)
    {
      return false;
    }

    if (pthread_join(thread_, returnValue) != 0)
    {
      return false;
    }

    started_ = false;
    return true;
  }

  bool detach()
  {
    if (!started_)
    {
      return false;
    }

    if (pthread_detach(thread_) != 0)
    {
      return false;
    }

    started_ = false;
    return true;
  }

  bool joinable() const
  {
    return started_;
  }

  pthread_t native_handle() const
  {
    return thread_;
  }

private:
  pthread_t thread_;
  bool started_;
};

} // namespace avengine
