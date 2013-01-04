/**
 * Copyright (C) 2005-2013 Christoph Rupp (chris@crupp.de).
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * See files COPYING.* for License information.
 */

#include "../src/config.h"

#include <stdexcept>
#include <ham/hamsterdb.hpp>

#include "bfc-testsuite.hpp"
#include "hamster_fixture.hpp"

using namespace bfc;

static int
my_compare_func(ham_db_t *db,
        const ham_u8_t *lhs, ham_size_t lhs_length,
        const ham_u8_t *rhs, ham_size_t rhs_length) {
  (void)db;
  (void)lhs;
  (void)rhs;
  (void)lhs_length;
  (void)rhs_length;
  return (0);
}

static int
my_prefix_compare_func(ham_db_t *db,
         const ham_u8_t *lhs, ham_size_t lhs_length,
         ham_size_t lhs_real_length,
         const ham_u8_t *rhs, ham_size_t rhs_length,
         ham_size_t rhs_real_length) {
  (void)db;
  (void)lhs;
  (void)rhs;
  (void)lhs_length;
  (void)rhs_length;
  (void)lhs_real_length;
  (void)rhs_real_length;
  return (0);
}

class CppApiTest : public hamsterDB_fixture {
public:
  CppApiTest()
    : hamsterDB_fixture("CppApiTest") {
    testrunner::get_instance()->register_fixture(this);
    BFC_REGISTER_TEST(CppApiTest, keyTest);
    BFC_REGISTER_TEST(CppApiTest, recordTest);
    BFC_REGISTER_TEST(CppApiTest, staticFunctionsTest);
    BFC_REGISTER_TEST(CppApiTest, compareTest);
    BFC_REGISTER_TEST(CppApiTest, createOpenCloseDbTest);
    BFC_REGISTER_TEST(CppApiTest, insertFindEraseTest);
    BFC_REGISTER_TEST(CppApiTest, cursorTest);
    BFC_REGISTER_TEST(CppApiTest, envTest);
    BFC_REGISTER_TEST(CppApiTest, envDestructorTest);
    BFC_REGISTER_TEST(CppApiTest, envGetDatabaseNamesTest);
    BFC_REGISTER_TEST(CppApiTest, getLicenseTest);
    BFC_REGISTER_TEST(CppApiTest, beginAbortTest);
    BFC_REGISTER_TEST(CppApiTest, beginCommitTest);
    BFC_REGISTER_TEST(CppApiTest, beginCursorAbortTest);
    BFC_REGISTER_TEST(CppApiTest, beginCursorCommitTest);
  }

public:
  void keyTest() {
    void *p = (void *)"123";
    void *q = (void *)"234";
    hamsterdb::key k1, k2(p, 4, HAM_KEY_USER_ALLOC);

    BFC_ASSERT_EQUAL((void *)0, k1.get_data());
    BFC_ASSERT_EQUAL((ham_size_t)0, k1.get_size());
    BFC_ASSERT_EQUAL((ham_u32_t)0, k1.get_flags());

    BFC_ASSERT_EQUAL(p, k2.get_data());
    BFC_ASSERT_EQUAL((ham_size_t)4, k2.get_size());
    BFC_ASSERT_EQUAL((ham_u32_t)HAM_KEY_USER_ALLOC, k2.get_flags());

    k1 = k2;
    BFC_ASSERT_EQUAL(p, k1.get_data());
    BFC_ASSERT_EQUAL((ham_size_t)4, k1.get_size());
    BFC_ASSERT_EQUAL((ham_u32_t)HAM_KEY_USER_ALLOC, k1.get_flags());

    hamsterdb::key k3(k1);
    BFC_ASSERT_EQUAL(p, k3.get_data());
    BFC_ASSERT_EQUAL((ham_size_t)4, k3.get_size());
    BFC_ASSERT_EQUAL((ham_u32_t)HAM_KEY_USER_ALLOC, k3.get_flags());

    int i = 3;
    hamsterdb::key k4;
    k4.set<int>(i);
    BFC_ASSERT_EQUAL((void *)&i, k4.get_data());
    BFC_ASSERT_EQUAL(sizeof(int), (size_t)k4.get_size());

    k1.set_data(q);
    k1.set_size(2);
    k1.set_flags(0);
    BFC_ASSERT_EQUAL(q, k1.get_data());
    BFC_ASSERT_EQUAL((ham_size_t)2, k1.get_size());
    BFC_ASSERT_EQUAL((ham_u32_t)0, k1.get_flags());
  }

  void recordTest() {
    void *p = (void *)"123";
    void *q = (void *)"234";
    hamsterdb::record r1, r2(p, 4, HAM_RECORD_USER_ALLOC);

    BFC_ASSERT_EQUAL((void *)0, r1.get_data());
    BFC_ASSERT_EQUAL((ham_size_t)0, r1.get_size());
    BFC_ASSERT_EQUAL((ham_u32_t)0, r1.get_flags());

    BFC_ASSERT_EQUAL(p, r2.get_data());
    BFC_ASSERT_EQUAL((ham_size_t)4, r2.get_size());
    BFC_ASSERT_EQUAL((ham_u32_t)HAM_RECORD_USER_ALLOC, r2.get_flags());

    r1=r2;
    BFC_ASSERT_EQUAL(p, r1.get_data());
    BFC_ASSERT_EQUAL((ham_size_t)4, r1.get_size());
    BFC_ASSERT_EQUAL((ham_u32_t)HAM_RECORD_USER_ALLOC, r1.get_flags());

    hamsterdb::record r3(r1);
    BFC_ASSERT_EQUAL(p, r3.get_data());
    BFC_ASSERT_EQUAL((ham_size_t)4, r3.get_size());
    BFC_ASSERT_EQUAL((ham_u32_t)HAM_RECORD_USER_ALLOC, r3.get_flags());

    r1.set_data(q);
    r1.set_size(2);
    r1.set_flags(0);
    BFC_ASSERT_EQUAL(q, r1.get_data());
    BFC_ASSERT_EQUAL((ham_size_t)2, r1.get_size());
    BFC_ASSERT_EQUAL((ham_u32_t)0, r1.get_flags());
  }

  void staticFunctionsTest() {
    hamsterdb::db db;
    // check for obvious errors

    db.get_version(0, 0, 0);
    BFC_ASSERT(".get_version() did not throw while receiving NULL arguments");
    db.get_license(0, 0);
    BFC_ASSERT(".get_license() did not throw while receiving NULL arguments");
  }

  void compareTest() {
    hamsterdb::env env;
    env.create(BFC_OPATH(".test"));
    hamsterdb::db db = env.create_db(1);
    db.set_compare_func(my_compare_func);
    db.set_prefix_compare_func(my_prefix_compare_func);
    env.close(HAM_AUTO_CLEANUP);
  }

  void createOpenCloseDbTest() {
    hamsterdb::env env;

    try {
      env.create("data/");
    }
    catch (hamsterdb::error &) {
    }

    env.create(BFC_OPATH(".test"));
    env.close();

    try {
      env.open("xxxxxx");
    }
    catch (hamsterdb::error &) {
    }

    env.open(BFC_OPATH(".test"));
    env = env;
    env.close();
  }

  void insertFindEraseTest() {
    hamsterdb::env env;
    hamsterdb::db db;
    hamsterdb::key k;
    hamsterdb::record r, out;

    k.set_data((void *)"12345");
    k.set_size(6);
    r.set_data((void *)"12345");
    r.set_size(6);

    env.create(BFC_OPATH(".test"));
    db = env.create_db(1);

    try {
      db.insert(0, &r);
    }
    catch (hamsterdb::error &) {
    }

    try {
      db.insert(&k, 0);
    }
    catch (hamsterdb::error &) {
    }

    db.insert(&k, &r);
    try {
      db.insert(&k, &r);  // already exists
    }
    catch (hamsterdb::error &) {
    }

    out = db.find(&k);
    BFC_ASSERT_EQUAL(r.get_size(), out.get_size());
    BFC_ASSERT_EQUAL(0, ::memcmp(r.get_data(), out.get_data(), out.get_size()));
    db.erase(&k);

    try {
      db.erase(0);
    }
    catch(hamsterdb::error &) {
    }

    try {
      db.erase(&k);
    }
    catch (hamsterdb::error &) {
    }

    try {
      out = db.find(&k);
    }
    catch (hamsterdb::error &e) {
      BFC_ASSERT_EQUAL(HAM_KEY_NOT_FOUND, e.get_errno());
      BFC_ASSERT_EQUAL(0, strcmp("Key not found", e.get_string()));
    }

    try {
      out = db.find(0);
    }
    catch (hamsterdb::error &) {
    }

    db.close();
    env.close();
    db.close();
    env.close();
    env.close();
    env.open(BFC_OPATH(".test"));
  }

  void cursorTest() {
    hamsterdb::env env;
    hamsterdb::db db;

    try {
      hamsterdb::cursor cerr(&db);
    }
    catch (hamsterdb::error &) {
    }

    hamsterdb::key k((void *)"12345", 5), k2;
    hamsterdb::record r((void *)"12345", 5), r2;

    env.create(BFC_OPATH(".test"));
    db = env.create_db(1);
    hamsterdb::cursor c(&db);
    c.create(&db); // overwrite

    c.insert(&k, &r);
    try {
      c.insert(&k, 0);
    }
    catch (hamsterdb::error &) {
    }
    try {
      c.insert(0, &r);
    }
    catch (hamsterdb::error &) {
    }
    try {
      c.insert(&k, &r);  // already exists
    }
    catch (hamsterdb::error &) {
    }
    try {
      c.overwrite(0);
    }
    catch (hamsterdb::error &) {
    }
    c.overwrite(&r);
    hamsterdb::cursor clone = c.clone();

    c.move_first(&k2, &r2);
    BFC_ASSERT_EQUAL(k.get_size(), k2.get_size());
    BFC_ASSERT_EQUAL(r.get_size(), r2.get_size());

    c.move_last(&k2, &r2);
    BFC_ASSERT_EQUAL(k.get_size(), k2.get_size());
    BFC_ASSERT_EQUAL(r.get_size(), r2.get_size());

    try {
      c.move_next();
    }
    catch (hamsterdb::error &e) {
      BFC_ASSERT_EQUAL(e.get_errno(), HAM_KEY_NOT_FOUND);
    }

    try {
      c.move_previous();
    }
    catch (hamsterdb::error &e) {
      BFC_ASSERT_EQUAL(e.get_errno(), HAM_KEY_NOT_FOUND);
    }

    c.find(&k);
    BFC_ASSERT_EQUAL((ham_u32_t)1, c.get_duplicate_count());

    c.erase();
    try {
      c.erase();
    }
    catch (hamsterdb::error &) {
    }

    try {
      c.find(&k);
    }
    catch (hamsterdb::error &) {
    }

    hamsterdb::cursor temp;
    temp.close();
  }

  void envTest() {
    hamsterdb::env env;

    env.create(BFC_OPATH(".test"));
    env.flush();
    env.close();
    env.close();
    env.close();
    env.open(BFC_OPATH(".test"));

    hamsterdb::db db1 = env.create_db(1);
    db1.close();
    db1 = env.open_db(1);
    env.rename_db(1, 2);

    try {
      env.erase_db(2);
    }
    catch (hamsterdb::error &e) {
      BFC_ASSERT_EQUAL(HAM_DATABASE_ALREADY_OPEN, e.get_errno());
    }
    db1.close();
    env.erase_db(2);
  }

  void envDestructorTest() {
    hamsterdb::db db1;
    hamsterdb::env env;

    env.create(BFC_OPATH(".test"));
    db1 = env.create_db(1);

    /* let the objects go out of scope */
  }

  void envGetDatabaseNamesTest() {
    hamsterdb::env env;
    std::vector<ham_u16_t> v;

    env.create(BFC_OPATH(".test"));

    v = env.get_database_names();
    BFC_ASSERT_EQUAL((ham_size_t)0, (ham_size_t)v.size());

    hamsterdb::db db1 = env.create_db(1);
    v = env.get_database_names();
    BFC_ASSERT_EQUAL((ham_size_t)1, (ham_size_t)v.size());
    BFC_ASSERT_EQUAL((ham_u16_t)1, v[0]);
    env.close();
  }

  void getLicenseTest() {
    const char *licensee = 0, *product = 0;

    hamsterdb::db::get_license(0, 0);
    hamsterdb::db::get_license(&licensee, 0);
    BFC_ASSERT(licensee != 0);
    hamsterdb::db::get_license(0, &product);
    BFC_ASSERT(product != 0);
    hamsterdb::db::get_license(&licensee, &product);
    BFC_ASSERT(licensee != 0);
    BFC_ASSERT(product != 0);
  }

  void beginAbortTest() {
    hamsterdb::env env;
    hamsterdb::db db;
    hamsterdb::key k;
    hamsterdb::record r, out;
    hamsterdb::txn txn;

    k.set_data((void *)"12345");
    k.set_size(6);
    r.set_data((void *)"12345");
    r.set_size(6);

    env.create(BFC_OPATH(".test"), HAM_ENABLE_TRANSACTIONS);
    db = env.create_db(1);
    txn = env.begin();
    db.insert(&txn, &k, &r);
    txn.abort();
    try {
      out = db.find(&k);
    }
    catch (hamsterdb::error &e) {
      BFC_ASSERT_EQUAL(HAM_KEY_NOT_FOUND, e.get_errno());
    }
  }

  void beginCommitTest() {
    hamsterdb::db db;
    hamsterdb::env env;
    hamsterdb::key k;
    hamsterdb::record r, out;
    hamsterdb::txn txn;

    k.set_data((void *)"12345");
    k.set_size(6);
    r.set_data((void *)"12345");
    r.set_size(6);

    env.create(BFC_OPATH(".test"), HAM_ENABLE_TRANSACTIONS);
    db = env.create_db(1);
    txn = env.begin("name");
    db.insert(&txn, &k, &r);
    std::string n = txn.get_name();
    BFC_ASSERT_EQUAL(0, strcmp("name", n.c_str()));
    txn.commit();
    out = db.find(&k);
  }

  void beginCursorAbortTest() {
    hamsterdb::env env;
    hamsterdb::db db;
    hamsterdb::key k;
    hamsterdb::record r, out;
    hamsterdb::txn txn;

    k.set_data((void *)"12345");
    k.set_size(6);
    r.set_data((void *)"12345");
    r.set_size(6);

    env.create(BFC_OPATH(".test"), HAM_ENABLE_TRANSACTIONS);
    db = env.create_db(1);
    txn = env.begin();
    hamsterdb::cursor c(&db, &txn);
    c.insert(&k, &r);
    BFC_ASSERT_EQUAL(r.get_size(), c.get_record_size());
    c.close();
    txn.abort();
    try {
      out=db.find(&k);
    }
    catch (hamsterdb::error &e) {
      BFC_ASSERT_EQUAL(HAM_KEY_NOT_FOUND, e.get_errno());
    }
  }

  void beginCursorCommitTest() {
    hamsterdb::env env;
    hamsterdb::db db;
    hamsterdb::key k;
    hamsterdb::record r, out;
    hamsterdb::txn txn;

    k.set_data((void *)"12345");
    k.set_size(6);
    r.set_data((void *)"12345");
    r.set_size(6);

    env.create(BFC_OPATH(".test"), HAM_ENABLE_TRANSACTIONS);
    db = env.create_db(1);
    txn = env.begin();
    hamsterdb::cursor c(&db, &txn);
    c.insert(&k, &r);
    c.close();
    txn.commit();
    out = db.find(&k);
  }

  /*
   * Augment the base method: make sure we catch hamsterdb::error exceptions
   * and convert these to bfc::error instances to assist BFC test error
   * reporting.
   *
   * This serves as an example of use of the testrunner configuration
   * as well, as we use the catch flags to determine if the user wants
   * us to catch these exceptions or allow them to fall through to the
   * debugger instead.
   */
  virtual bool FUT_invoker(testrunner *me, method m, const char *funcname,
      bfc_state_t state, bfc::error &ex) {
    if (me->catch_exceptions() || me->catch_coredumps()) {
      try {
        // invoke the FUT through the baseclass method
        return fixture::FUT_invoker(me, m, funcname, state, ex);
      }
      catch (hamsterdb::error &e) {
        ex = bfc::error(__FILE__, __LINE__, get_name(), funcname,
                  "HAM C++ exception occurred within the "
                  "Function-Under-Test (%s); error code %d: %s",
          funcname, (int)e.get_errno(), e.get_string());
        return true;
      }
      // catch (bfc::error &e)
      // ^^ do NOT catch those: allow the BFC test rig to catch 'em!
    }
    else {
      // invoke the FUT through the baseclass method
      return fixture::FUT_invoker(me, m, funcname, state, ex);
    }
  }
};

BFC_REGISTER_FIXTURE(CppApiTest);
