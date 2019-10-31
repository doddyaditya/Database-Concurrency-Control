// Author: Alexander Thomson (thomson@cs.yale.edu)

#include "txn/txn.h"

#include <string>

#include "txn/txn_processor.h"
#include "txn/txn_types.h"
#include "utils/testing.h"

TEST(NoopTest) {
  TxnProcessor p(SERIAL);

  Txn* t = new Noop();
  EXPECT_EQ(INCOMPLETE, t->Status());

  p.NewTxnRequest(t);
  p.GetTxnResult();

  EXPECT_EQ(COMMITTED, t->Status());
  delete t;

  END;
}

TEST(PutTest) {
  TxnProcessor p(SERIAL);
  Txn* t;

  p.NewTxnRequest(new Put("1", "2"));
  delete p.GetTxnResult();

  p.NewTxnRequest(new Expect("0", "2"));  // Should abort (no key '0' exists)
  t = p.GetTxnResult();
  EXPECT_EQ(ABORTED, t->Status());
  delete t;

  p.NewTxnRequest(new Expect("1", "1"));  // Should abort (wrong value for key)
  t = p.GetTxnResult();
  EXPECT_EQ(ABORTED, t->Status());
  delete t;

  p.NewTxnRequest(new Expect("1", "2"));  // Should commit
  t = p.GetTxnResult();
  EXPECT_EQ(COMMITTED, t->Status());
  delete t;

  END;
}

TEST(PutMultipleTest) {
  TxnProcessor p(SERIAL);
  Txn* t;

  map<Key, Value> m;
  for (int i = 0; i < 1000; i++)
    m[IntToString(i)] = IntToString(i*i);

  p.NewTxnRequest(new PutMultiple(m));
  delete p.GetTxnResult();

  p.NewTxnRequest(new ExpectMultiple(m));
  t = p.GetTxnResult();
  EXPECT_EQ(COMMITTED, t->Status());
  delete t;

  END;
}

int main(int argc, char** argv) {
  NoopTest();
  PutTest();
  PutMultipleTest();
}

