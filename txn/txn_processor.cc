// Author: Alexander Thomson (thomson@cs.yale.edu)
// Modified by: Christina Wallin (christina.wallin@yale.edu)
// Modified by: Kun Ren (kun.ren@yale.edu)


#include "txn/txn_processor.h"
#include <stdio.h>
#include <set>

#include "txn/lock_manager.h"

// Thread & queue counts for StaticThreadPool initialization.
#define THREAD_COUNT 8

TxnProcessor::TxnProcessor(CCMode mode)
    : mode_(mode), tp_(THREAD_COUNT), next_unique_id_(1) {
  if (mode_ == LOCKING_EXCLUSIVE_ONLY)
    lm_ = new LockManagerA(&ready_txns_);
  else if (mode_ == LOCKING)
    lm_ = new LockManagerB(&ready_txns_);

  // Create the storage
  if (mode_ == MVCC) {
    storage_ = new MVCCStorage();
  } else {
    storage_ = new Storage();
  }

  storage_->InitStorage();

  // Start 'RunScheduler()' running.
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
  pthread_t scheduler_;
  pthread_create(&scheduler_, &attr, StartScheduler, reinterpret_cast<void*>(this));

}

void* TxnProcessor::StartScheduler(void * arg) {
  reinterpret_cast<TxnProcessor *>(arg)->RunScheduler();
  return NULL;
}

TxnProcessor::~TxnProcessor() {
  if (mode_ == LOCKING_EXCLUSIVE_ONLY || mode_ == LOCKING)
    delete lm_;

  delete storage_;
}

void TxnProcessor::NewTxnRequest(Txn* txn) {
  // Atomically assign the txn a new number and add it to the incoming txn
  // requests queue.
  mutex_.Lock();
  txn->unique_id_ = next_unique_id_;
  next_unique_id_++;
  txn_requests_.Push(txn);
  mutex_.Unlock();
}

Txn* TxnProcessor::GetTxnResult() {
  Txn* txn;
  while (!txn_results_.Pop(&txn)) {
    // No result yet. Wait a bit before trying again (to reduce contention on
    // atomic queues).
    sleep(0.000001);
  }
  return txn;
}

void TxnProcessor::RunScheduler() {
  switch (mode_) {
    case SERIAL:                 RunSerialScheduler(); break;
    case LOCKING:                RunLockingScheduler(); break;
    case LOCKING_EXCLUSIVE_ONLY: RunLockingScheduler(); break;
    case OCC:                    RunOCCScheduler(); break;
    case P_OCC:                  RunOCCParallelScheduler(); break;
    case MVCC:                   RunMVCCScheduler();
  }
}

void TxnProcessor::RunSerialScheduler() {
  Txn* txn;
  while (tp_.Active()) {
    // Get next txn request.
    if (txn_requests_.Pop(&txn)) {
      // Execute txn.
      ExecuteTxn(txn);

      // Commit/abort txn according to program logic's commit/abort decision.
      if (txn->Status() == COMPLETED_C) {
        ApplyWrites(txn);
        txn->status_ = COMMITTED;
      } else if (txn->Status() == COMPLETED_A) {
        txn->status_ = ABORTED;
      } else {
        // Invalid TxnStatus!
        DIE("Completed Txn has invalid TxnStatus: " << txn->Status());
      }

      // Return result to client.
      txn_results_.Push(txn);
    }
  }
}

void TxnProcessor::RunLockingScheduler() {
  // Sets the transation variable
  Txn* txn;
  // As long as the transaction variable is active...
  while (tp_.Active()) {
    // Start processing the next incoming transaction request.
    if (txn_requests_.Pop(&txn)) {
      bool blocked = false;
      // Request read locks.
      for (set<Key>::iterator it = txn->readset_.begin();
           it != txn->readset_.end(); ++it) {
        if (!lm_->ReadLock(txn, *it)) {
          blocked = true;
          // If readset_.size() + writeset_.size() > 1, and blocked, just abort
          if (txn->readset_.size() + txn->writeset_.size() > 1) {
            // Release all locks that already acquired
            for (set<Key>::iterator it_reads = txn->readset_.begin(); true; ++it_reads) {
              lm_->Release(txn, *it_reads);
              if (it_reads == it) {
                break;
              }
            }
            break;
          }
        }
      }

      if (blocked == false) {
        // Request write locks.
        for (set<Key>::iterator it = txn->writeset_.begin();
             it != txn->writeset_.end(); ++it) {
          if (!lm_->WriteLock(txn, *it)) {
            blocked = true;
            // If readset_.size() + writeset_.size() > 1, and blocked, just abort
            if (txn->readset_.size() + txn->writeset_.size() > 1) {
              // Release all read locks that already acquired
              for (set<Key>::iterator it_reads = txn->readset_.begin(); it_reads != txn->readset_.end(); ++it_reads) {
                lm_->Release(txn, *it_reads);
              }
              // Release all write locks that already acquired
              for (set<Key>::iterator it_writes = txn->writeset_.begin(); true; ++it_writes) {
                lm_->Release(txn, *it_writes);
                if (it_writes == it) {
                  break;
                }
              }
              break;
            }
          }
        }
      }

      // If all read and write locks were immediately acquired, this txn is
      // ready to be executed. Else, just restart the txn
      if (blocked == false) {
        ready_txns_.push_back(txn);
      } else if (blocked == true && (txn->writeset_.size() + txn->readset_.size() > 1)){
        mutex_.Lock();
        txn->unique_id_ = next_unique_id_;
        next_unique_id_++;
        txn_requests_.Push(txn);
        mutex_.Unlock();
      }
    }

    // Process and commit all transactions that have finished running.
    while (completed_txns_.Pop(&txn)) {
      // Commit/abort txn according to program logic's commit/abort decision.
      if (txn->Status() == COMPLETED_C) {
        ApplyWrites(txn);
        txn->status_ = COMMITTED;
      } else if (txn->Status() == COMPLETED_A) {
        txn->status_ = ABORTED;
      } else {
        // Invalid TxnStatus!
        DIE("Completed Txn has invalid TxnStatus: " << txn->Status());
      }

      // Release read locks.
      for (set<Key>::iterator it = txn->readset_.begin();
           it != txn->readset_.end(); ++it) {
        lm_->Release(txn, *it);
      }
      // Release write locks.
      for (set<Key>::iterator it = txn->writeset_.begin();
           it != txn->writeset_.end(); ++it) {
        lm_->Release(txn, *it);
      }

      // Return result to client.
      txn_results_.Push(txn);
    }

    // Start executing all transactions that have newly acquired all their
    // locks.
    while (ready_txns_.size()) {
      // Get next ready txn from the queue.
      txn = ready_txns_.front();
      ready_txns_.pop_front();

      // Start txn running in its own thread.
      tp_.RunTask(new Method<TxnProcessor, void, Txn*>(
            this,
            &TxnProcessor::ExecuteTxn,
            txn));

    }
  }
}

void TxnProcessor::ExecuteTxn(Txn* txn) {

  // Get the start time
  txn->occ_start_time_ = GetTime();

  // Read everything in from readset.
  for (set<Key>::iterator it = txn->readset_.begin();
       it != txn->readset_.end(); ++it) {
    // Save each read result iff record exists in storage.
    Value result;
    if (storage_->Read(*it, &result))
      txn->reads_[*it] = result;
  }

  // Also read everything in from writeset.
  for (set<Key>::iterator it = txn->writeset_.begin();
       it != txn->writeset_.end(); ++it) {
    // Save each read result iff record exists in storage.
    Value result;
    if (storage_->Read(*it, &result))
      txn->reads_[*it] = result;
  }

  // Execute txn's program logic.
  txn->Run();

  // Hand the txn back to the RunScheduler thread.
  completed_txns_.Push(txn);
}

void TxnProcessor::ApplyWrites(Txn* txn) {
  // Write buffered writes out to storage.
  for (map<Key, Value>::iterator it = txn->writes_.begin();
       it != txn->writes_.end(); ++it) {
    storage_->Write(it->first, it->second, txn->unique_id_);
  }
}

/**
 * Precondition: No storage writes are occuring during execution.
 */
bool TxnProcessor::OCCValidateTransaction(const Txn &txn) const {
  for (auto&& key : txn.readset_) {
    if (txn.occ_start_time_ < storage_->Timestamp(key))
      return false;
  }

  for (auto&& key : txn.writeset_) {
    if (txn.occ_start_time_ < storage_->Timestamp(key))
      return false;
  }

  return true;
}

void TxnProcessor::RunOCCScheduler() {
  // Fetch transaction requests, and immediately begin executing them.
  Txn *txn;

  // While the transaction is active
  while (tp_.Active()) {
    // If there is request, pop it -> assign it to txn variable
    if (txn_requests_.Pop(&txn)) {
      // Start txn running in its own thread, then run the transaction
      txn->occ_start_time_ = GetTime();
      tp_.RunTask(new Method<TxnProcessor, void, Txn*>(this, &TxnProcessor::ExecuteTxn,txn));
    }

    // Check every finished transaction done by the request
    // All transaction done by ExecuteTxn is put into the completed_txns_ queue
    Txn *finishedTask;
    bool valid = true;
    while (completed_txns_.Pop(&finishedTask)) {

      // Validating transaction
      // All the timestamp of the resources read and write must be bigger than the transactions start time

      // Check read time...
      for (set<Key>::iterator itr = finishedTask->readset_.begin(); itr != finishedTask->readset_.end(); itr++) {
        if (storage_->Timestamp(*itr) > finishedTask->occ_start_time_) {
          valid = false;
          break;
        }
      }

      // Check write time...
      for (set<Key>::iterator itr = finishedTask->writeset_.begin(); itr != finishedTask->writeset_.end(); itr++) {
        if (storage_->Timestamp(*itr) > finishedTask->occ_start_time_) {
          valid = false;
          break;
        }
      }

      // If the transaction is aborted...
      // No need to run it again. Just put it out...
      // Set the status to commited
      if (finishedTask->Status() == COMPLETED_A) {
        finishedTask->status_ = ABORTED;
      }
      // If transaction is valid and not aborted => Write the write commands
      // Set the status to COMMITTED
      else if (valid && finishedTask->Status() == COMPLETED_C) {
        // Write key and value for every finishedTask reads and writes before 
        for (map<Key, Value>::iterator itr = finishedTask->writes_.begin(); itr != finishedTask->writes_.end(); ++itr) {
          storage_->Write(itr->first, itr->second, finishedTask->unique_id_);
        }
        finishedTask->status_ = COMMITTED;
      }
      // Else if the task is COMMITED but not valid
      // Redo the task again
      else if (!valid && finishedTask->Status() == COMPLETED_C) {
        // Set empty reads and writes
        finishedTask->reads_.empty();
        finishedTask->writes_.empty();
        finishedTask->status_ = INCOMPLETE;

        // Try transaction again
        NewTxnRequest(finishedTask);
        continue;
      }
      
      // Else...
      // That means the task status is invalid.
      // KILL
      else {
        // Invalid TxnStatus!
        DIE("Completed Txn has invalid TxnStatus: " << txn->Status());
      }
      txn_results_.Push(finishedTask);
    }
  }
}

void TxnProcessor::RunOCCParallelScheduler() {
  // CPSC 438/538:
  //
  // Implement this method! Note that implementing OCC with parallel
  // validation may need to create another method, like
  // TxnProcessor::ExecuteTxnParallel.
  // Note that you can use active_set_ and active_set_mutex_ we provided
  // for you in the txn_processor.h
  //
  // [For now, run serial scheduler in order to make it through the test
  // suite]
  RunSerialScheduler();
}

void TxnProcessor::RunMVCCScheduler() {
  // CPSC 438/538:
  //
  // Implement this method!

  // Hint:Pop a txn from txn_requests_, and pass it to a thread to execute.
  // Note that you may need to create another execute method, like TxnProcessor::MVCCExecuteTxn.
  //
  // [For now, run serial scheduler in order to make it through the test
  // suite]

  //SUSAH BANGET BINGUNG AKUTUH
  Txn* txn;
  while (tp_.Active()) {
    // Start processing the next incoming transaction request.
    if (txn_requests_.Pop(&txn)) {
      tp_.RunTask(new Method<TxnProcessor, void, Txn*>(
            this,
            &TxnProcessor::MVCCExecuteTxn,
            txn));
    }
  }
  txn_results_.Push(txn);
}
 
void TxnProcessor::MVCCExecuteTxn(Txn* txn) {
  //1. Read all necessary data for this transaction from storage (Note that you should lock the key before each read)
  for (set<Key>::iterator it = txn->readset_.begin();
       it != txn->readset_.end(); ++it) {
    Value result;
    //lock before read
    storage_->Lock(*it);
    if (storage_->Read(*it, &result)) {
      txn->reads_[*it] = result;
    }
    storage_->Unlock(*it);
  }

  // Also read everything in from writeset.
  for (set<Key>::iterator it = txn->writeset_.begin();
       it != txn->writeset_.end(); ++it) {
    // Save each read result iff record exists in storage.
    Value result;
    //lock before read
    storage_->Lock(*it);
    if (storage_->Read(*it, &result)) {
     
      txn->reads_[*it] = result;
    }   
    storage_->Unlock(*it);
  }

  //2. Execute the transaction logic (i.e. call Run() on the transaction)
    
  txn->Run();

  //3. Acquire all locks for keys in the write_set_
  for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it) {
    storage_->Lock(*it);
  }

  //4. Call MVCCStorage::CheckWrite method to check all keys in the write_set_
    
  bool verified = false;
  for (set<Key>::iterator it = txn->writeset_.begin();
       it != txn->writeset_.end(); ++it) {
    if (storage_->CheckWrite(*it, txn->unique_id_)) {
      verified = true;
    }
    else {
      verified =  false;
      break;
    }
  }

  //5. If (each key passed the check)
    
  if (verified) {  
    //6. Apply the writes
    ApplyWrites(txn);

    //7.Release all locks for keys in the write_set_
    for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it) {
      storage_->Unlock(*it);
    }
  }

  // 8. else if (at least one key failed the check)
  else {
    //9. Release all locks for keys in the write_set_
    for (set<Key>::iterator it = txn->writeset_.begin(); it != txn->writeset_.end(); ++it) {
      storage_->Unlock(*it);
    }
    
    //10. Cleanup txn
    txn->reads_.empty();
    txn->writes_.empty();
    txn->status_ = INCOMPLETE;

    //11. Completely restart the transaction
    NewTxnRequest(txn);

  }
  // Hand the txn back to the RunScheduler thread.
  completed_txns_.Push(txn);
}



