/// @file
/// @author Alexander Thomson <thomson@cs.yale.edu>
// Modified by: Kun Ren (kun.ren@yale.edu)

#ifndef _DB_UTILS_STATIC_THREAD_POOL_H_
#define _DB_UTILS_STATIC_THREAD_POOL_H_

#include "pthread.h"
#include "stdlib.h"
#include "assert.h"
#include <queue>
#include <string>
#include <vector>
#include <utility>
#include "utils/atomic.h"
#include "utils/thread_pool.h"

using std::queue;
using std::string;
using std::vector;
using std::pair;

//
class StaticThreadPool : public ThreadPool {
 public:
  StaticThreadPool(int nthreads)
      : thread_count_(nthreads), stopped_(false) {
    Start();
  }


  ~StaticThreadPool() {
    stopped_ = true;
    for (int i = 0; i < thread_count_; i++)
      pthread_join(threads_[i], NULL);
  }

  bool Active() { return !stopped_; }

  virtual void RunTask(Task* task) {
    assert(!stopped_);
    while (!queues_[rand() % thread_count_].PushNonBlocking(task)) {}
  }

  virtual int ThreadCount() { return thread_count_; }

 private:
  void Start() {
    threads_.resize(thread_count_);
    queues_.resize(thread_count_);
    
    // Pin all threads in the thread pool to CPU Core 0 ~ 6
    cpu_set_t cpuset;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    CPU_ZERO(&cpuset);
    CPU_SET(0, &cpuset);
    CPU_SET(1, &cpuset);       
    CPU_SET(2, &cpuset);
    CPU_SET(3, &cpuset);
    CPU_SET(4, &cpuset);
    CPU_SET(5, &cpuset);
    CPU_SET(6, &cpuset);            
    pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);

    for (int i = 0; i < thread_count_; i++) {
      pthread_create(&threads_[i],
                     &attr,
                     RunThread,
                     reinterpret_cast<void*>(new pair<int, StaticThreadPool*>(i, this)));
    }
  }

  // Function executed by each pthread.
  static void* RunThread(void* arg) {
    int queue_id = reinterpret_cast<pair<int, StaticThreadPool*>*>(arg)->first;
    StaticThreadPool* tp = reinterpret_cast<pair<int, StaticThreadPool*>*>(arg)->second;
    
    Task* task;
    int sleep_duration = 1;  // in microseconds
    while (true) {
      if (tp->queues_[queue_id].PopNonBlocking(&task)) {
        task->Run();
        delete task;
        // Reset backoff.
        sleep_duration = 1;
      } else {
        usleep(sleep_duration);
        // Back off exponentially.
        if (sleep_duration < 32)
          sleep_duration *= 2;
      }

      if (tp->stopped_) {
        // Go through ALL queues looking for a remaining task.
        while (tp->queues_[queue_id].Pop(&task)) {
            task->Run();
            delete task;
        }

        break;
      }
    }
    return NULL;
  }

  int thread_count_;
  vector<pthread_t> threads_;

  // Task queues.
  vector<AtomicQueue<Task*> > queues_;

  bool stopped_;
};

#endif  // _DB_UTILS_STATIC_THREAD_POOL_H_

