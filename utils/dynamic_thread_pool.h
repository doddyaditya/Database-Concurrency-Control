/// @file
/// @author Alexander Thomson <thomson@cs.yale.edu>

#ifndef _DB_UTILS_DYNAMIC_THREAD_POOL_H_
#define _DB_UTILS_DYNAMIC_THREAD_POOL_H_

#include "pthread.h"
#include "stdlib.h"
#include "assert.h"
#include <queue>
#include <string>
#include <vector>
#include "utils/atomic.h"
#include "utils/task.h"
#include "utils/mutex.h"
#include "utils/condition.h"
#include "utils/thread_pool.h"

#include <iostream>
using namespace std;

using std::queue;
using std::string;
using std::vector;

class DynamicThreadPool : public ThreadPool {
 public:
  // Default constructor creates a ThreadPool with no threads.
  DynamicThreadPool() : thread_count_(0) {}

  virtual void RunTask(Task* task) {
    // Pop an available thread off the queue.
    Thread* thread;
    if (!available_threads_.Pop(&thread)) {
      // Create a new available thread if there aren't any.
      thread = new Thread(this);
      ++thread_count_;
    }

    // Run the task in the available thread we just grabbed.
    thread->RunTask(task);
  }

  virtual int ThreadCount() { return *thread_count_; }

 private:
  class Thread {
   public:
    Thread(DynamicThreadPool* tp) : thread_pool_(tp), task_(NULL) {
      pthread_create(&pthread_,
                     NULL,
                     StaticRunThread,
                     reinterpret_cast<void*>(this));
    }

    void RunTask(Task* task) {
      assert(task_ == NULL);
      task_ = task;

      // Signal thread to wake up (unless it has already executed the task).
      cv_.SignalIfNonNull<Task*>(&task_);
    }

   private:
    static void* StaticRunThread(void* arg) {
      reinterpret_cast<Thread*>(arg)->RunThread();
      return NULL;
    };

    void RunThread() {
      while (true) {
        // Run task_ any time it's not NULL.
        cv_.WaitWhileEq<Task*>(NULL, &task_);
        task_->Run();

        // 
        delete task_;
        task_ = NULL;
        thread_pool_->available_threads_.Push(this);
      }
    }

    void Kill() {
    }

    DynamicThreadPool* thread_pool_;
    Task* task_;
    pthread_t pthread_;
    Condition cv_;
  };

  // Available thread queue.
  AtomicQueue<Thread*> available_threads_;

  Atomic<int> thread_count_;
};

#endif  // _DB_UTILS_DYNAMIC_THREAD_POOL_H_

