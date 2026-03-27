#pragma once

namespace avengine
{

template <typename MutexT> class lock_guard
{
  
public:

  explicit lock_guard(MutexT& mutex) : mutex_(mutex)
  {
    mutex_.lock();
  }

  ~lock_guard()
  {
    mutex_.unlock();
  }

  lock_guard(const lock_guard&) = delete;

  lock_guard& operator=(const lock_guard&) = delete;

private:
  MutexT& mutex_;
  
};

} // namespace avengine
