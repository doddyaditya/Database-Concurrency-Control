// Author: Alexander Thomson (thomson@cs.yale.edu)
// Modified by: Kun Ren (kun.ren@yale.edu)
//
// Lock manager implementing deterministic two-phase locking as described in
// 'The Case for Determinism in Database Systems'.

#include <deque>

#include "txn/lock_manager.h"

using std::deque;

LockManager::~LockManager() {
  // Cleanup lock_table_
  for (auto it = lock_table_.begin(); it != lock_table_.end(); it++) {
    delete it->second;
  }
}

deque<LockManager::LockRequest>* LockManager::_getLockQueue(const Key& key) {
  deque<LockRequest> *dq = lock_table_[key];
  if (!dq) {
    dq = new deque<LockRequest>();
    lock_table_[key] = dq;
  }
  return dq;
}

LockManagerA::LockManagerA(deque<Txn*>* ready_txns) {
  ready_txns_ = ready_txns;
}

bool LockManagerA::WriteLock(Txn* txn, const Key& key) {
  // New lock request (set as EXCLUSIVE) for the transaction
  LockRequest lockreq(EXCLUSIVE, txn);

  // If the lock queue for the current element is empty, then create a new lock request deque
  // If not then push the lock into the last element of the deque
  if (lock_table_[key]){
    lock_table_[key]->push_back(lockreq);
  } else {
    deque<LockRequest> *tempdeque = new deque<LockRequest>(1, lockreq);
    lock_table_[key] = tempdeque;
  }

  // If the lock table size is 1, then the write is successful
  // Else the transaction needs to wait longer to be served
  if(lock_table_[key]->size() == 1){
    return true;
  }
  txn_waits_[txn]++;
  return false;
}

bool LockManagerA::ReadLock(Txn* txn, const Key& key) {
  // Since Part 1A implements ONLY exclusive locks, calls to ReadLock can
  // simply use the same logic as 'WriteLock'.
  return WriteLock(txn, key);
}

void LockManagerA::Release(Txn* txn, const Key& key) {
  // Add the deque for the requested resource
  // Assume that the unlock has happened (deleteHappened)
  deque<LockRequest> *tempQueue = lock_table_[key];
  bool deleteHappened = true;

  // Loop the deque from the beginning until the end
  // If the transaction is still in the queue
  // Delete the transaction from the lock request deque
  // Delete happened for the current transaction if the transaction is at the front
  //  of the deque (first element of the lock request deque)
  for (deque<LockRequest>::iterator itr = tempQueue->begin(); itr < tempQueue->end(); ++itr) {
    if (itr->txn_ == txn) { 
        tempQueue->erase(itr);
        break;
    }
    deleteHappened = false;
  }

  // If the deque is still not empty after the deletion of the transaction
  // The next transaction is set as the first element of the deque and reduce the wait time
  // If the transaction wait time for the next transaction is 0 => next transaction is served
  if (!(tempQueue->size() < 1) && deleteHappened) {
    Txn *next_transaction = tempQueue->front().txn_;
    txn_waits_[next_transaction]--;

    if (txn_waits_[next_transaction] <= 0) {
        ready_txns_->push_back(next_transaction);
        txn_waits_.erase(next_transaction);
    }
  }
}

LockMode LockManagerA::Status(const Key& key, vector<Txn*>* owners) {
  // Checking if the lock request queue for certain resource is 0
  // If it is, then the resource is UNLOCKED => return
  deque<LockRequest> *tempQueue = lock_table_[key];
  if (!tempQueue->size()) {
    return UNLOCKED;
  }

  // If not, then set the clear current owners
  // Change the owner with the transaction at the front of the lock request queue
  // EXCLUSIVE => return
  owners->clear();
  owners->push_back(tempQueue->front().txn_);
  return EXCLUSIVE;
}

LockManagerB::LockManagerB(deque<Txn*>* ready_txns) {
  ready_txns_ = ready_txns;
}

bool LockManagerB::_addLock(LockMode mode, Txn* txn, const Key& key) {
  LockRequest rq(mode, txn);
  LockMode status = Status(key, nullptr);

  deque<LockRequest> *dq = _getLockQueue(key);
  dq->push_back(rq);

  bool granted = status == UNLOCKED;
  if (mode == SHARED) {
    granted |= _noExclusiveWaiting(key);
  } else {
    _numExclusiveWaiting[key]++;
  }

  if (!granted)
    txn_waits_[txn]++;

  return granted;
}


bool LockManagerB::WriteLock(Txn* txn, const Key& key) {
  return _addLock(EXCLUSIVE, txn, key);
}

bool LockManagerB::ReadLock(Txn* txn, const Key& key) {
  return _addLock(SHARED, txn, key);
}

void LockManagerB::Release(Txn* txn, const Key& key) {
  deque<LockRequest> *queue = _getLockQueue(key);

  for (auto it = queue->begin(); it < queue->end(); it++) {
    if (it->txn_ == txn) {
      queue->erase(it);
      if (it->mode_ == EXCLUSIVE) {
        _numExclusiveWaiting[key]--;
      }

      break;
    }
  }

  // Advance the lock, by making new owners ready.
  // Some in newOwners already own the lock.  These are not in
  // txn_waits_.
  vector<Txn*> newOwners;
  Status(key, &newOwners);

  for (auto&& owner : newOwners) {
    auto waitCount = txn_waits_.find(owner);
    if (waitCount != txn_waits_.end() && --(waitCount->second) == 0) {
      ready_txns_->push_back(owner);
      txn_waits_.erase(waitCount);
    }
  }
}

LockMode LockManagerB::Status(const Key& key, vector<Txn*>* owners) {
  deque<LockRequest> *dq = _getLockQueue(key);
  if (dq->empty()) {
    return UNLOCKED;
  }

  LockMode mode = EXCLUSIVE;
  vector<Txn*> txn_owners;
  for (auto&& lockRequest : *dq) {
    if (lockRequest.mode_ == EXCLUSIVE && mode == SHARED)
        break;

    txn_owners.push_back(lockRequest.txn_);
    mode = lockRequest.mode_;

    if (mode == EXCLUSIVE)
      break;
  }

  if (owners)
    *owners = txn_owners;

  return mode;
}

inline bool LockManagerB::_noExclusiveWaiting(const Key& key) {
  return _numExclusiveWaiting[key] == 0;
}
