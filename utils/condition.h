/// @file
/// @author Alexander Thomson <thomson@cs.yale.edu>

#ifndef _DB_UTILS_CONDITION_H_
#define _DB_UTILS_CONDITION_H_

#include <pthread.h>
#include "utils/mutex.h"
#include "utils/task.h"

/// @class Condition
///
/// A condition variable implementation that facilitates waiting and signalling
/// between multiple threads.
class Condition {
 public:
  /// To ensure atomicity, Condition variables are always associated with a
  /// mutex. When the default constructor is used, the Condition object owns
  /// its own Mutex.
  Condition() : m_(new Mutex()), own_mutex_(true) {
    pthread_cond_init(&cv_, NULL);
  }

  /// A Condition can also be created that uses an existing Mutex. When created
  /// in this way, a Condition variable will not delete the Mutex when its
  /// destructor is called.
  Condition(Mutex* m) : m_(m), own_mutex_(false) {
    pthread_cond_init(&cv_, NULL);
  }

  ~Condition() {
    if (own_mutex_)
      delete m_;
  }

  /// Puts the calling thread to sleep until another thread signals on this
  /// Condition variable.
  inline void Wait() {
    m_->Lock();
    pthread_cond_wait(&cv_, &m_->mutex_);
    m_->Unlock();
  }

  /// Signals all threads waiting on the condition variable to wake up and
  /// continue execution.
  inline void Signal() {
    m_->Lock();
    pthread_cond_signal(&cv_);
    m_->Unlock();
  }

#define WAIT_WHILE(a) \
  m_->Lock(); \
  while (a) \
    pthread_cond_wait(&cv_, &m_->mutex_); \
  m_->Unlock()

  inline void WaitWhileTrue(bool* b) { WAIT_WHILE(*b); }
  inline void WaitWhileFalse(bool* b) { WAIT_WHILE(!*b); }
  template<typename T>
  inline void WaitWhileNull(T** p) { WAIT_WHILE(*p == NULL); }
  template<typename T>
  inline void WaitWhileNonNull(T** p) { WAIT_WHILE(*p != NULL); }
  template<typename T>
  inline void WaitWhileEq(const T& exp, T* act) { WAIT_WHILE(*act == exp); }
  template<typename T>
  inline void WaitWhileGt(const T& exp, T* act) { WAIT_WHILE(*act > exp); }
  template<typename T>
  inline void WaitWhileGe(const T& exp, T* act) { WAIT_WHILE(*act >= exp); }
  template<typename T>
  inline void WaitWhileLt(const T& exp, T* act) { WAIT_WHILE(*act < exp); }
  template<typename T>
  inline void WaitWhileLe(const T& exp, T* act) { WAIT_WHILE(*act <= exp); }

#undef WAIT_WHILE

#define SIGNAL_IF(a) \
  m_->Lock(); \
  bool r = (a); \
  if (r) \
    pthread_cond_signal(&cv_); \
  m_->Unlock(); \
  return r

  inline bool SignalIfTrue(bool* b) { SIGNAL_IF(*b); }
  inline bool SignalIfFalse(bool* b) { SIGNAL_IF(!*b); }
  template<typename T>
  inline bool SignalIfNull(T* p) { SIGNAL_IF(*p == NULL); }
  template<typename T>
  inline bool SignalIfNonNull(T* p) { SIGNAL_IF(*p != NULL); }
  template<typename T>
  inline bool SignalIfEq(const T& exp, T* act) { SIGNAL_IF(*act == exp); }
  template<typename T>
  inline bool SignalIfGt(const T& exp, T* act) { SIGNAL_IF(*act > exp); }
  template<typename T>
  inline bool SignalIfGe(const T& exp, T* act) { SIGNAL_IF(*act >= exp); }
  template<typename T>
  inline bool SignalIfLt(const T& exp, T* act) { SIGNAL_IF(*act < exp); }
  template<typename T>
  inline bool SignalIfLe(const T& exp, T* act) { SIGNAL_IF(*act <= exp); }

#undef SIGNAL_IF

  inline bool SignalIf(RTask<bool>* task) {
    bool r;
    task->SetResultPointer(&r);

    m_->Lock();
    task->Run();
    if (r)
      pthread_cond_signal(&cv_);
    m_->Unlock();
    return r;
  }

 private:
  // Pointer to mutex associated with this condition variable.
  Mutex* m_;
  bool own_mutex_;
  pthread_cond_t cv_;
};

#endif  // _DB_UTILS_CONDITION_H_

