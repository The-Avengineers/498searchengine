#pragma once

#include <pthread.h>

namespace avengine
{

// A readers-writer lock. Multiple threads may hold the shared (read) lock
// simultaneously; the exclusive (write) lock is held by at most one thread
// with no concurrent readers.
class SharedMutex
{
public:
  SharedMutex()
  {
    pthread_rwlock_init(&rwlock_, nullptr);
  }

  ~SharedMutex()
  {
    pthread_rwlock_destroy(&rwlock_);
  }

  SharedMutex(const SharedMutex&) = delete;
  SharedMutex& operator=(const SharedMutex&) = delete;

  // Exclusive (write) lock - compatible with lock_guard<shared_mutex>
  void lock()
  {
    pthread_rwlock_wrlock(&rwlock_);
  }
  void unlock()
  {
    pthread_rwlock_unlock(&rwlock_);
  }

  // Shared (read) lock
  void lockShared()
  {
    pthread_rwlock_rdlock(&rwlock_);
  }
  void unlockShared()
  {
    pthread_rwlock_unlock(&rwlock_);
  }

private:
  pthread_rwlock_t rwlock_{};
};

// RAII wrapper that acquires the shared (read) side of a shared_mutex on
// construction and releases it on destruction.
template <typename MutexT> class SharedLockGuard
{
public:
  explicit SharedLockGuard(MutexT& m) : m_(m)
  {
    m_.lockShared();
  }
  ~SharedLockGuard()
  {
    m_.unlockShared();
  }

  SharedLockGuard(const SharedLockGuard&) = delete;
  SharedLockGuard& operator=(const SharedLockGuard&) = delete;

private:
  MutexT& m_;
};

} // namespace avengine
