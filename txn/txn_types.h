// Author: Alexander Thomson (thomson@cs.yale.edu)
// Modified by: Kun Ren (kun.ren@yale.edu)

#ifndef _TXN_TYPES_H_
#define _TXN_TYPES_H_

#include <map>
#include <set>
#include <string>

#include "txn/txn.h"

// Immediately commits.
class Noop : public Txn {
 public:
  Noop() {}
  virtual void Run() { COMMIT; }

  Noop* clone() const {             // Virtual constructor (copying)
    Noop* clone = new Noop();
    this->CopyTxnInternals(clone);
    return clone;
  }
};

// Reads all keys in the map 'm', if all results correspond to the values in
// the provided map, commits, else aborts.
class Expect : public Txn {
 public:
  Expect(const map<Key, Value>& m) : m_(m) {
    for (map<Key, Value>::iterator it = m_.begin(); it != m_.end(); ++it)
      readset_.insert(it->first);
  }

  Expect* clone() const {             // Virtual constructor (copying)
    Expect* clone = new Expect(map<Key, Value>(m_));
    this->CopyTxnInternals(clone);
    return clone;
  }

  virtual void Run() {
    Value result;
    for (map<Key, Value>::iterator it = m_.begin(); it != m_.end(); ++it) {
      if (!Read(it->first, &result) || result != it->second) {
        ABORT;
      }
    }
    COMMIT;
  }

 private:
  map<Key, Value> m_;
};

// Inserts all pairs in the map 'm'.
class Put : public Txn {
 public:
  Put(const map<Key, Value>& m) : m_(m) {
    for (map<Key, Value>::iterator it = m_.begin(); it != m_.end(); ++it)
      writeset_.insert(it->first);
  }

  Put* clone() const {             // Virtual constructor (copying)
    Put* clone = new Put(map<Key, Value>(m_));
    this->CopyTxnInternals(clone);
    return clone;
  }

  virtual void Run() {
    for (map<Key, Value>::iterator it = m_.begin(); it != m_.end(); ++it)
      Write(it->first, it->second);
    COMMIT;
  }

 private:
  map<Key, Value> m_;
};

// Read-modify-write transaction.
class RMW : public Txn {
 public:
  explicit RMW(double time = 0) : time_(time) {}
  RMW(const set<Key>& writeset, double time = 0) : time_(time) {
    writeset_ = writeset;
  }
  RMW(const set<Key>& readset, const set<Key>& writeset, double time = 0)
      : time_(time) {
    readset_ = readset;
    writeset_ = writeset;
  }

  // Constructor with randomized read/write sets
  RMW(int dbsize, int readsetsize, int writesetsize, double time = 0)
      : time_(time) {
    // Make sure we can find enough unique keys.
    DCHECK(dbsize >= readsetsize + writesetsize);

    // Find readsetsize unique read keys.
    for (int i = 0; i < readsetsize; i++) {
      Key key;
      do {
        key = rand() % dbsize;
      } while (readset_.count(key));
      readset_.insert(key);
    }

    // Find writesetsize unique write keys.
    for (int i = 0; i < writesetsize; i++) {
      Key key;
      do {
        key = rand() % dbsize;
      } while (readset_.count(key) || writeset_.count(key));
      writeset_.insert(key);
    }
  }

  RMW* clone() const {             // Virtual constructor (copying)
    RMW* clone = new RMW(time_);
    this->CopyTxnInternals(clone);
    return clone;
  }

  virtual void Run() {
    Value result;
    // Read everything in readset.
    for (set<Key>::iterator it = readset_.begin(); it != readset_.end(); ++it)
      Read(*it, &result);

    // Increment length of everything in writeset.
    for (set<Key>::iterator it = writeset_.begin(); it != writeset_.end();
         ++it) {
      result = 0;
      Read(*it, &result);
      Write(*it, result + 1);
    }

    // Run while loop to simulate the txn logic(duration is time_).
    double begin = GetTime();
    while (GetTime() - begin < time_) {
      for (int i = 0;i < 1000; i++) {
        int x = 100;
        x = x + 2;
        x = x*x;
      }
    }
    
    COMMIT;
  }

 private:
  double time_;
};

#endif  // _TXN_TYPES_H_

