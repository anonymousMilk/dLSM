/* Copyright (c) 2011 The dLSM Authors. All rights reserved.
   Use of this source code is governed by a BSD-style license that can be
   found in the LICENSE file. See the AUTHORS file for names of contributors. */

#include "dLSM/c.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char* phase = "";

static void StartPhase(const char* name) {
  fprintf(stderr, "=== Test %s\n", name);
  phase = name;
}

#define CheckNoError(err)                                               \
  if ((err) != NULL) {                                                  \
    fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, phase, (err)); \
    abort();                                                            \
  }

#define CheckCondition(cond)                                            \
  if (!(cond)) {                                                        \
    fprintf(stderr, "%s:%d: %s: %s\n", __FILE__, __LINE__, phase, #cond); \
    abort();                                                            \
  }

static void CheckEqual(const char* expected, const char* v, size_t n) {
  if (expected == NULL && v == NULL) {
    // ok
  } else if (expected != NULL && v != NULL && n == strlen(expected) &&
             memcmp(expected, v, n) == 0) {
    // ok
    return;
  } else {
    fprintf(stderr, "%s: expected '%s', got '%s'\n",
            phase,
            (expected ? expected : "(null)"),
            (v ? v : "(null"));
    abort();
  }
}

static void Free(char** ptr) {
  if (*ptr) {
    free(*ptr);
    *ptr = NULL;
  }
}

static void CheckGet(
    dLSM_t* db,
    const dLSM_readoptions_t* options,
    const char* key,
    const char* expected) {
  char* err = NULL;
  size_t val_len;
  char* val;
  val = dLSM_get(db, options, key, strlen(key), &val_len, &err);
  CheckNoError(err);
  CheckEqual(expected, val, val_len);
  Free(&val);
}

static void CheckIter(dLSM_iterator_t* iter,
                      const char* key, const char* val) {
  size_t len;
  const char* str;
  str = dLSM_iter_key(iter, &len);
  CheckEqual(key, str, len);
  str = dLSM_iter_value(iter, &len);
  CheckEqual(val, str, len);
}

// Callback from dLSM_writebatch_iterate()
static void CheckPut(void* ptr,
                     const char* k, size_t klen,
                     const char* v, size_t vlen) {
  int* state = (int*) ptr;
  CheckCondition(*state < 2);
  switch (*state) {
    case 0:
      CheckEqual("bar", k, klen);
      CheckEqual("b", v, vlen);
      break;
    case 1:
      CheckEqual("box", k, klen);
      CheckEqual("c", v, vlen);
      break;
  }
  (*state)++;
}

// Callback from dLSM_writebatch_iterate()
static void CheckDel(void* ptr, const char* k, size_t klen) {
  int* state = (int*) ptr;
  CheckCondition(*state == 2);
  CheckEqual("bar", k, klen);
  (*state)++;
}

static void CmpDestroy(void* arg) { }

static int CmpCompare(void* arg, const char* a, size_t alen,
                      const char* b, size_t blen) {
  int n = (alen < blen) ? alen : blen;
  int r = memcmp(a, b, n);
  if (r == 0) {
    if (alen < blen) r = -1;
    else if (alen > blen) r = +1;
  }
  return r;
}

static const char* CmpName(void* arg) {
  return "foo";
}

// Custom filter policy
static uint8_t fake_filter_result = 1;
static void FilterDestroy(void* arg) { }
static const char* FilterName(void* arg) {
  return "TestFilter";
}
static char* FilterCreate(
    void* arg,
    const char* const* key_array, const size_t* key_length_array,
    int num_keys,
    size_t* filter_length) {
  *filter_length = 4;
  char* result = malloc(4);
  memcpy(result, "fake", 4);
  return result;
}
uint8_t FilterKeyMatch(void* arg, const char* key, size_t length,
                       const char* filter, size_t filter_length) {
  CheckCondition(filter_length == 4);
  CheckCondition(memcmp(filter, "fake", 4) == 0);
  return fake_filter_result;
}

int main(int argc, char** argv) {
  dLSM_t* db;
  dLSM_comparator_t* cmp;
  dLSM_cache_t* cache;
  dLSM_env_t* env;
  dLSM_options_t* options;
  dLSM_readoptions_t* roptions;
  dLSM_writeoptions_t* woptions;
  char* dbname;
  char* err = NULL;
  int run = -1;

  CheckCondition(dLSM_major_version() >= 1);
  CheckCondition(dLSM_minor_version() >= 1);

  StartPhase("create_objects");
  cmp = dLSM_comparator_create(NULL, CmpDestroy, CmpCompare, CmpName);
  env = dLSM_create_default_env();
  cache = dLSM_cache_create_lru(100000);
  dbname = dLSM_env_get_test_directory(env);
  CheckCondition(dbname != NULL);

  options = dLSM_options_create();
  dLSM_options_set_comparator(options, cmp);
  dLSM_options_set_error_if_exists(options, 1);
  dLSM_options_set_cache(options, cache);
  dLSM_options_set_env(options, env);
  dLSM_options_set_info_log(options, NULL);
  dLSM_options_set_write_buffer_size(options, 100000);
  dLSM_options_set_paranoid_checks(options, 1);
  dLSM_options_set_max_open_files(options, 10);
  dLSM_options_set_block_size(options, 1024);
  dLSM_options_set_block_restart_interval(options, 8);
  dLSM_options_set_max_file_size(options, 3 << 20);
  dLSM_options_set_compression(options, dLSM_no_compression);

  roptions = dLSM_readoptions_create();
  dLSM_readoptions_set_verify_checksums(roptions, 1);
  dLSM_readoptions_set_fill_cache(roptions, 0);

  woptions = dLSM_writeoptions_create();
  dLSM_writeoptions_set_sync(woptions, 1);

  StartPhase("destroy");
  dLSM_destroy_db(options, dbname, &err);
  Free(&err);

  StartPhase("open_error");
  db = dLSM_open(options, dbname, &err);
  CheckCondition(err != NULL);
  Free(&err);

  StartPhase("dLSM_free");
  db = dLSM_open(options, dbname, &err);
  CheckCondition(err != NULL);
  dLSM_free(err);
  err = NULL;

  StartPhase("open");
  dLSM_options_set_create_if_missing(options, 1);
  db = dLSM_open(options, dbname, &err);
  CheckNoError(err);
  CheckGet(db, roptions, "foo", NULL);

  StartPhase("put");
  dLSM_put(db, woptions, "foo", 3, "hello", 5, &err);
  CheckNoError(err);
  CheckGet(db, roptions, "foo", "hello");

  StartPhase("compactall");
  dLSM_compact_range(db, NULL, 0, NULL, 0);
  CheckGet(db, roptions, "foo", "hello");

  StartPhase("compactrange");
  dLSM_compact_range(db, "a", 1, "z", 1);
  CheckGet(db, roptions, "foo", "hello");

  StartPhase("writebatch");
  {
    dLSM_writebatch_t* wb = dLSM_writebatch_create();
    dLSM_writebatch_put(wb, "foo", 3, "a", 1);
    dLSM_writebatch_clear(wb);
    dLSM_writebatch_put(wb, "bar", 3, "b", 1);
    dLSM_writebatch_put(wb, "box", 3, "c", 1);

    dLSM_writebatch_t* wb2 = dLSM_writebatch_create();
    dLSM_writebatch_delete(wb2, "bar", 3);
    dLSM_writebatch_append(wb, wb2);
    dLSM_writebatch_destroy(wb2);

    dLSM_write(db, woptions, wb, &err);
    CheckNoError(err);
    CheckGet(db, roptions, "foo", "hello");
    CheckGet(db, roptions, "bar", NULL);
    CheckGet(db, roptions, "box", "c");

    int pos = 0;
    dLSM_writebatch_iterate(wb, &pos, CheckPut, CheckDel);
    CheckCondition(pos == 3);
    dLSM_writebatch_destroy(wb);
  }

  StartPhase("iter");
  {
    dLSM_iterator_t* iter = dLSM_create_iterator(db, roptions);
    CheckCondition(!dLSM_iter_valid(iter));
    dLSM_iter_seek_to_first(iter);
    CheckCondition(dLSM_iter_valid(iter));
    CheckIter(iter, "box", "c");
    dLSM_iter_next(iter);
    CheckIter(iter, "foo", "hello");
    dLSM_iter_prev(iter);
    CheckIter(iter, "box", "c");
    dLSM_iter_prev(iter);
    CheckCondition(!dLSM_iter_valid(iter));
    dLSM_iter_seek_to_last(iter);
    CheckIter(iter, "foo", "hello");
    dLSM_iter_seek(iter, "b", 1);
    CheckIter(iter, "box", "c");
    dLSM_iter_get_error(iter, &err);
    CheckNoError(err);
    dLSM_iter_destroy(iter);
  }

  StartPhase("approximate_sizes");
  {
    int i;
    int n = 20000;
    char keybuf[100];
    char valbuf[100];
    uint64_t sizes[2];
    const char* start[2] = { "a", "k00000000000000010000" };
    size_t start_len[2] = { 1, 21 };
    const char* limit[2] = { "k00000000000000010000", "z" };
    size_t limit_len[2] = { 21, 1 };
    dLSM_writeoptions_set_sync(woptions, 0);
    for (i = 0; i < n; i++) {
      snprintf(keybuf, sizeof(keybuf), "k%020d", i);
      snprintf(valbuf, sizeof(valbuf), "v%020d", i);
      dLSM_put(db, woptions, keybuf, strlen(keybuf), valbuf, strlen(valbuf),
                  &err);
      CheckNoError(err);
    }
    dLSM_approximate_sizes(db, 2, start, start_len, limit, limit_len, sizes);
    CheckCondition(sizes[0] > 0);
    CheckCondition(sizes[1] > 0);
  }

  StartPhase("property");
  {
    char* prop = dLSM_property_value(db, "nosuchprop");
    CheckCondition(prop == NULL);
    prop = dLSM_property_value(db, "dLSM.stats");
    CheckCondition(prop != NULL);
    Free(&prop);
  }

  StartPhase("snapshot");
  {
    const dLSM_snapshot_t* snap;
    snap = dLSM_create_snapshot(db);
    dLSM_delete(db, woptions, "foo", 3, &err);
    CheckNoError(err);
    dLSM_readoptions_set_snapshot(roptions, snap);
    CheckGet(db, roptions, "foo", "hello");
    dLSM_readoptions_set_snapshot(roptions, NULL);
    CheckGet(db, roptions, "foo", NULL);
    dLSM_release_snapshot(db, snap);
  }

  StartPhase("repair");
  {
    dLSM_close(db);
    dLSM_options_set_create_if_missing(options, 0);
    dLSM_options_set_error_if_exists(options, 0);
    dLSM_repair_db(options, dbname, &err);
    CheckNoError(err);
    db = dLSM_open(options, dbname, &err);
    CheckNoError(err);
    CheckGet(db, roptions, "foo", NULL);
    CheckGet(db, roptions, "bar", NULL);
    CheckGet(db, roptions, "box", "c");
    dLSM_options_set_create_if_missing(options, 1);
    dLSM_options_set_error_if_exists(options, 1);
  }

  StartPhase("filter");
  for (run = 0; run < 2; run++) {
    // First run uses custom filter, second run uses bloom filter
    CheckNoError(err);
    dLSM_filterpolicy_t* policy;
    if (run == 0) {
      policy = dLSM_filterpolicy_create(
          NULL, FilterDestroy, FilterCreate, FilterKeyMatch, FilterName);
    } else {
      policy = dLSM_filterpolicy_create_bloom(10);
    }

    // Create new database
    dLSM_close(db);
    dLSM_destroy_db(options, dbname, &err);
    dLSM_options_set_filter_policy(options, policy);
    db = dLSM_open(options, dbname, &err);
    CheckNoError(err);
    dLSM_put(db, woptions, "foo", 3, "foovalue", 8, &err);
    CheckNoError(err);
    dLSM_put(db, woptions, "bar", 3, "barvalue", 8, &err);
    CheckNoError(err);
    dLSM_compact_range(db, NULL, 0, NULL, 0);

    fake_filter_result = 1;
    CheckGet(db, roptions, "foo", "foovalue");
    CheckGet(db, roptions, "bar", "barvalue");
    if (phase == 0) {
      // Must not find value when custom filter returns false
      fake_filter_result = 0;
      CheckGet(db, roptions, "foo", NULL);
      CheckGet(db, roptions, "bar", NULL);
      fake_filter_result = 1;

      CheckGet(db, roptions, "foo", "foovalue");
      CheckGet(db, roptions, "bar", "barvalue");
    }
    dLSM_options_set_filter_policy(options, NULL);
    dLSM_filterpolicy_destroy(policy);
  }

  StartPhase("cleanup");
  dLSM_close(db);
  dLSM_options_destroy(options);
  dLSM_readoptions_destroy(roptions);
  dLSM_writeoptions_destroy(woptions);
  dLSM_free(dbname);
  dLSM_cache_destroy(cache);
  dLSM_comparator_destroy(cmp);
  dLSM_env_destroy(env);

  fprintf(stderr, "PASS\n");
  return 0;
}
