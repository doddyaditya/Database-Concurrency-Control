// Author: Kun Ren (kun.ren@yale.edu)
// Modified by Daniel Abadi

#include "txn/mvcc_storage.h"

// Init the storage
void MVCCStorage::InitStorage() {
  for (int i = 0; i < 1000000;i++) {
    Write(i, 0, 0);
    Mutex* key_mutex = new Mutex();
    mutexs_[i] = key_mutex;
  }
}

// Free memory.
MVCCStorage::~MVCCStorage() {
  for (unordered_map<Key, deque<Version*>*>::iterator it = mvcc_data_.begin();
       it != mvcc_data_.end(); ++it) {
    delete it->second;          
  }
  
  mvcc_data_.clear();
  
  for (unordered_map<Key, Mutex*>::iterator it = mutexs_.begin();
       it != mutexs_.end(); ++it) {
    delete it->second;          
  }
  
  mutexs_.clear();
}

// Lock the key to protect its version_list. Remember to lock the key when you read/update the version_list 
void MVCCStorage::Lock(Key key) {
  mutexs_[key]->Lock();
}

// Unlock the key.
void MVCCStorage::Unlock(Key key) {
  mutexs_[key]->Unlock();
}

// MVCC Read
bool MVCCStorage::Read(Key key, Value* result, int txn_unique_id) {
  // CPSC 638:
	//
  // Hint: Iterate the version_lists and return the verion whose write timestamp
  // (version_id) is the largest write timestamp less than or equal to txn_unique_id.
	//
	

	if(mvcc_data_.count(key) == 0){
		DIE("Couldn't find key " << key);
		return false;
	}
	
	deque<Version*>* versions = mvcc_data_[key];

	if(versions == NULL)
		DIE("Version list is null for key " << key);

	// Make sure we found the best write timestamp
	bool found_one = false;
	Version* best;

	// Start searching
	for (deque<Version*>::iterator it = versions->begin(); it!=versions->end(); ++it){
		Version* v = *it;

		if(v == NULL){
			DIE("Key " << key << " is null, transaction: " << txn_unique_id);
		}

		if(v->version_id_ <= txn_unique_id){
			found_one = true;
			best = v;
			break;
		}
	}

	// Make sure we actually found one
	if(found_one){
		*result = best->value_;
		if(best->max_read_id_ < txn_unique_id)
			best->max_read_id_ = txn_unique_id;
		return true;
	}else
		return false;
}



// Check whether apply or abort the write
bool MVCCStorage::CheckWrite(Key key, int txn_unique_id) {
  // CPSC 638:
  //
  // Implement this method!
	
  // Hint: Before all writes are applied, we need to make sure that each write
  // can be safely applied based on MVCC timestamp ordering protocol. This method
  // only checks one key, so you should call this method for each key in the
  // write_set. Return true if this key passes the check, return false if not. 
  // Note that you don't have to call Lock(key) in this method, just
  // call Lock(key) before you call this method and call Unlock(key) afterward.
	
	if(mvcc_data_.count(key) == 0){
		DIE("Bad key: " << key);
	}
	
	deque<Version*>* versions = mvcc_data_[key];
	
	for (deque<Version*>::iterator it = versions->begin(); it!=versions->end(); ++it){
		Version* v = *it;
		
		if(v == NULL)
			DIE("Key " << key << " is null...");

		if(v->version_id_ > txn_unique_id || v->max_read_id_ > txn_unique_id)
			return false;
	}
  return true;
}

// MVCC Write, call this method only if CheckWrite return true.
void MVCCStorage::Write(Key key, Value value, int txn_unique_id) {
  // CPSC 638:
  //
  // Implement this method!
  
  // Hint: Insert a new version (malloc a Version and specify its value/version_id/max_read_id)
  // into the version_lists. Note that InitStorage() also calls this method to init storage. 
  // Note that you don't have to call Lock(key) in this method, just
  // call Lock(key) before you call this method and call Unlock(key) afterward.
  // Note that the performance would be much better if you organize the versions in decreasing order.
	//
	Version* update = (Version*) malloc(sizeof(Version));
	update->value_ 				= value;
	update->version_id_ 	= txn_unique_id;
	update->max_read_id_ 	= 0;

	deque<Version*>* versions;

	if(mvcc_data_.count(key) > 0){
		versions = mvcc_data_[key];
	}else{
		versions = new deque<Version*>();
		mvcc_data_[key] = versions;
	}
	// unordered_map<Key, deque<Version*>*>::const_iterator i;
	// 
	// i = mvcc_data_.find(key);

	// Is it being used for initialization?
	// if(i == mvcc_data_.end()){
	// 	deque<Version*>* init = new deque<Version*>();
	// 	mvcc_data_.insert(make_pair(key, init));
	// 	i = mvcc_data_.find(key);

	// 	// sanity check!
	// 	if(i == mvcc_data_.end()) DIE("Couldn't add key " << key);
	// }
	// 
	// deque<Version*>* versions = i->second;

	versions->push_front(update);
}


