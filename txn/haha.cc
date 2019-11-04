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
  Value result;
  for (set<Key>::iterator itr = txn->readset_.begin(); itr != txn->readset_.end(); itr++) {

    //lock before read
    storage_->Lock(*itr);
    if (storage_->Read(*itr, &result, txn->unique_id_)) {
      txn->reads_[*itr] = result;
    }

    // unlock:
    storage_->Unlock(*itr);
  }

  // Also read everything in from writeset.
  for (set<Key>::iterator itr = txn->writeset_.begin();
       itr != txn->writeset_.end(); itr++) {
    // Save each read result iff record exists in storage.
    //Value result;
    //lock before read
    storage_->Lock(*itr);
    if (storage_->Read(*itr, &result, txn->unique_id_)) {
      txn->reads_[*itr] = result;
    }

    // unlock:
    storage_->Unlock(*itr);
  }

  //2. Execute the transaction logic (i.e. call Run() on the transaction)

  txn->Run();

  //3. Acquire all locks for ALL keys in the write_set_
  for (set<Key>::iterator itr = txn->writeset_.begin(); itr != txn->writeset_.end(); itr++) {
    storage_->Lock(*itr);
  }

  //4. Call MVCCStorage::CheckWrite method to check all keys in the write_set_

  bool verified = false;
  for (set<Key>::iterator itr = txn->writes_.begin();
       itr != txn->writes_.end(); itr++) {
    if (storage_->CheckWrite(*itr, txn->unique_id_)) {
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
    // Hand the txn back to the RunScheduler thread.
    completed_txns_.Push(txn);
  }
  // 8. else if (at least one key failed the check)
  else {
    //9. Release all locks for keys in the write_set_
    for (set<Key>::iterator itr = txn->writeset_.begin(); itr != txn->writeset_.end(); itr++) {
      storage_->Unlock(*itr);
    }

    //10. Cleanup txn
    txn->reads_.empty();
    txn->writes_.empty();
    txn->status_ = INCOMPLETE;

    //11. Completely restart the transaction
    NewTxnRequest(txn);

  }

}



