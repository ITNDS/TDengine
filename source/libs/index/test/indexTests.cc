/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#include <gtest/gtest.h>
#include <iostream>
#include <string>
#include "index.h"
#include "indexInt.h"
#include "index_fst.h"
#include "index_fst_counting_writer.h"
#include "index_fst_util.h"
#include "index_tfile.h"
#include "tutil.h"

class FstWriter {
 public:
  FstWriter() {
    _wc = writerCtxCreate(TFile, "/tmp/tindex", false, 64 * 1024 * 1024);
    _b = fstBuilderCreate(NULL, 0);
  }
  bool Put(const std::string& key, uint64_t val) {
    FstSlice skey = fstSliceCreate((uint8_t*)key.c_str(), key.size());
    bool     ok = fstBuilderInsert(_b, skey, val);
    fstSliceDestroy(&skey);
    return ok;
  }
  ~FstWriter() {
    fstBuilderFinish(_b);
    fstBuilderDestroy(_b);

    writerCtxDestroy(_wc);
  }

 private:
  FstBuilder* _b;
  WriterCtx*  _wc;
};

class FstReadMemory {
 public:
  FstReadMemory(size_t size) {
    _wc = writerCtxCreate(TFile, "/tmp/tindex", true, 64 * 1024);
    _w = fstCountingWriterCreate(_wc);
    _size = size;
    memset((void*)&_s, 0, sizeof(_s));
  }
  bool init() {
    char* buf = (char*)calloc(1, sizeof(char) * _size);
    int   nRead = fstCountingWriterRead(_w, (uint8_t*)buf, _size);
    if (nRead <= 0) { return false; }
    _size = nRead;
    _s = fstSliceCreate((uint8_t*)buf, _size);
    _fst = fstCreate(&_s);
    free(buf);
    return _fst != NULL;
  }
  bool Get(const std::string& key, uint64_t* val) {
    FstSlice skey = fstSliceCreate((uint8_t*)key.c_str(), key.size());
    bool     ok = fstGet(_fst, &skey, val);
    fstSliceDestroy(&skey);
    return ok;
  }
  bool GetWithTimeCostUs(const std::string& key, uint64_t* val, uint64_t* elapse) {
    int64_t s = taosGetTimestampUs();
    bool    ok = this->Get(key, val);
    int64_t e = taosGetTimestampUs();
    *elapse = e - s;
    return ok;
  }
  // add later
  bool Search(AutomationCtx* ctx, std::vector<uint64_t>& result) {
    FstStreamBuilder*      sb = fstSearch(_fst, ctx);
    StreamWithState*       st = streamBuilderIntoStream(sb);
    StreamWithStateResult* rt = NULL;

    while ((rt = streamWithStateNextWith(st, NULL)) != NULL) {
      result.push_back((uint64_t)(rt->out.out));
    }
    return true;
  }
  bool SearchWithTimeCostUs(AutomationCtx* ctx, std::vector<uint64_t>& result) {
    int64_t s = taosGetTimestampUs();
    bool    ok = this->Search(ctx, result);
    int64_t e = taosGetTimestampUs();
    return ok;
  }

  ~FstReadMemory() {
    fstCountingWriterDestroy(_w);
    fstDestroy(_fst);
    fstSliceDestroy(&_s);
    writerCtxDestroy(_wc);
  }

 private:
  FstCountingWriter* _w;
  Fst*               _fst;
  FstSlice           _s;
  WriterCtx*         _wc;
  size_t             _size;
};

// TEST(IndexTest, index_create_test) {
//  SIndexOpts *opts = indexOptsCreate();
//  SIndex *index = indexOpen(opts, "./test");
//  if (index == NULL) {
//    std::cout << "index open failed" << std::endl;
//  }
//
//
//  // write
//  for (int i = 0; i < 100000; i++) {
//    SIndexMultiTerm* terms = indexMultiTermCreate();
//    std::string val = "field";
//
//    indexMultiTermAdd(terms, "tag1", strlen("tag1"), val.c_str(), val.size());
//
//    val.append(std::to_string(i));
//    indexMultiTermAdd(terms, "tag2", strlen("tag2"), val.c_str(), val.size());
//
//    val.insert(0, std::to_string(i));
//    indexMultiTermAdd(terms, "tag3", strlen("tag3"), val.c_str(), val.size());
//
//    val.append("const");
//    indexMultiTermAdd(terms, "tag4", strlen("tag4"), val.c_str(), val.size());
//
//
//    indexPut(index, terms, i);
//    indexMultiTermDestroy(terms);
//  }
//
//
//  // query
//  SIndexMultiTermQuery *multiQuery = indexMultiTermQueryCreate(MUST);
//
//  indexMultiTermQueryAdd(multiQuery, "tag1", strlen("tag1"), "field", strlen("field"), QUERY_PREFIX);
//  indexMultiTermQueryAdd(multiQuery, "tag3", strlen("tag3"), "0field0", strlen("0field0"), QUERY_TERM);
//
//  SArray *result = (SArray *)taosArrayInit(10, sizeof(int));
//  indexSearch(index, multiQuery, result);
//
//  std::cout << "taos'size : " << taosArrayGetSize(result) << std::endl;
//  for (int i = 0;  i < taosArrayGetSize(result); i++) {
//    int *v = (int *)taosArrayGet(result, i);
//    std::cout << "value --->" << *v  << std::endl;
//  }
//  // add more test case
//  indexMultiTermQueryDestroy(multiQuery);
//
//  indexOptsDestroy(opts);
//  indexClose(index);
//  //
//}

#define L 100
#define M 100
#define N 100

int Performance_fstWriteRecords(FstWriter* b) {
  std::string str("aa");
  for (int i = 0; i < L; i++) {
    str[0] = 'a' + i;
    str.resize(2);
    for (int j = 0; j < M; j++) {
      str[1] = 'a' + j;
      str.resize(2);
      for (int k = 0; k < N; k++) {
        str.push_back('a');
        b->Put(str, k);
        printf("(%d, %d, %d, %s)\n", i, j, k, str.c_str());
      }
    }
  }
  return L * M * N;
}

void Performance_fstReadRecords(FstReadMemory* m) {
  std::string str("aa");
  for (int i = 0; i < M; i++) {
    str[0] = 'a' + i;
    str.resize(2);
    for (int j = 0; j < N; j++) {
      str[1] = 'a' + j;
      str.resize(2);
      for (int k = 0; k < L; k++) {
        str.push_back('a');
        uint64_t val, cost;
        if (m->GetWithTimeCostUs(str, &val, &cost)) {
          printf("succes to get kv(%s, %" PRId64 "), cost: %" PRId64 "\n", str.c_str(), val, cost);
        } else {
          printf("failed to get key: %s\n", str.c_str());
        }
      }
    }
  }
}
void checkFstPerf() {
  FstWriter* fw = new FstWriter;
  int64_t    s = taosGetTimestampUs();

  int     num = Performance_fstWriteRecords(fw);
  int64_t e = taosGetTimestampUs();
  printf("write %d record cost %" PRId64 "us\n", num, e - s);
  delete fw;

  FstReadMemory* m = new FstReadMemory(1024 * 64);
  if (m->init()) { printf("success to init fst read"); }
  Performance_fstReadRecords(m);
  delete m;
}

void checkFstPrefixSearch() {
  FstWriter*  fw = new FstWriter;
  int64_t     s = taosGetTimestampUs();
  int         count = 2;
  std::string key("ab");

  for (int i = 0; i < count; i++) {
    key[1] = key[1] + i;
    fw->Put(key, i);
  }
  int64_t e = taosGetTimestampUs();

  std::cout << "insert data count :  " << count << "elapas time: " << e - s << std::endl;
  delete fw;

  FstReadMemory* m = new FstReadMemory(1024 * 64);
  if (m->init() == false) {
    std::cout << "init readMemory failed" << std::endl;
    delete m;
    return;
  }

  // prefix search
  std::vector<uint64_t> result;

  AutomationCtx* ctx = automCtxCreate((void*)"ab", AUTOMATION_PREFIX);
  m->Search(ctx, result);
  assert(result.size() == count);
  for (int i = 0; i < result.size(); i++) {
    assert(result[i] == i);  // check result
  }

  free(ctx);
  delete m;
}
void validateFst() {
  int        val = 100;
  int        count = 100;
  FstWriter* fw = new FstWriter;
  // write
  {
    std::string key("ab");
    for (int i = 0; i < count; i++) {
      key.push_back('a' + i);
      fw->Put(key, val - i);
    }
  }
  delete fw;

  // read
  FstReadMemory* m = new FstReadMemory(1024 * 64);
  if (m->init() == false) {
    std::cout << "init readMemory failed" << std::endl;
    delete m;
    return;
  }

  {
    std::string key("ab");
    uint64_t    out;
    if (m->Get(key, &out)) {
      printf("success to get (%s, %" PRId64 ")\n", key.c_str(), out);
    } else {
      printf("failed to get(%s)\n", key.c_str());
    }
    for (int i = 0; i < count; i++) {
      key.push_back('a' + i);
      if (m->Get(key, &out)) {
        assert(val - i == out);
        printf("success to get (%s, %" PRId64 ")\n", key.c_str(), out);
      } else {
        printf("failed to get(%s)\n", key.c_str());
      }
    }
  }
  delete m;
}

class IndexEnv : public ::testing::Test {
 protected:
  virtual void SetUp() {
    taosRemoveDir(path);
    opts = indexOptsCreate();
    int ret = indexOpen(opts, path, &index);
    assert(ret == 0);
  }
  virtual void TearDown() {
    indexClose(index);
    indexOptsDestroy(opts);
  }

  const char* path = "/tmp/tindex";
  SIndexOpts* opts;
  SIndex*     index;
};

// TEST_F(IndexEnv, testPut) {
//  // single index column
//  {
//    std::string colName("tag1"), colVal("Hello world");
//    SIndexTerm* term = indexTermCreate(0, ADD_VALUE, TSDB_DATA_TYPE_BINARY, colName.c_str(), colName.size(), colVal.c_str(),
//    colVal.size()); SIndexMultiTerm* terms = indexMultiTermCreate(); indexMultiTermAdd(terms, term);
//
//    for (size_t i = 0; i < 100; i++) {
//      int tableId = i;
//      int ret = indexPut(index, terms, tableId);
//      assert(ret == 0);
//    }
//    indexMultiTermDestroy(terms);
//  }
//  // multi index column
//  {
//    SIndexMultiTerm* terms = indexMultiTermCreate();
//    {
//      std::string colName("tag1"), colVal("Hello world");
//      SIndexTerm* term =
//          indexTermCreate(0, ADD_VALUE, TSDB_DATA_TYPE_BINARY, colName.c_str(), colName.size(), colVal.c_str(), colVal.size());
//      indexMultiTermAdd(terms, term);
//    }
//    {
//      std::string colName("tag2"), colVal("Hello world");
//      SIndexTerm* term =
//          indexTermCreate(0, ADD_VALUE, TSDB_DATA_TYPE_BINARY, colName.c_str(), colName.size(), colVal.c_str(), colVal.size());
//      indexMultiTermAdd(terms, term);
//    }
//
//    for (int i = 0; i < 100; i++) {
//      int tableId = i;
//      int ret = indexPut(index, terms, tableId);
//      assert(ret == 0);
//    }
//    indexMultiTermDestroy(terms);
//  }
//  //
//}

class IndexTFileEnv : public ::testing::Test {
 protected:
  virtual void SetUp() {
    taosRemoveDir(dir);
    taosMkDir(dir);
    tfInit();
    std::string colName("voltage");
    header.suid = 1;
    header.version = 1;
    memcpy(header.colName, colName.c_str(), colName.size());
    header.colType = TSDB_DATA_TYPE_BINARY;

    std::string path(dir);
    int         colId = 2;
    char        buf[64] = {0};
    sprintf(buf, "%" PRIu64 "-%d-%d.tindex", header.suid, colId, header.version);
    path.append("/").append(buf);

    ctx = writerCtxCreate(TFile, path.c_str(), false, 64 * 1024 * 1024);

    twrite = tfileWriterCreate(ctx, &header);
  }

  virtual void TearDown() {
    // indexClose(index);
    // indexeptsDestroy(opts);
    tfCleanup();
    tfileWriterDestroy(twrite);
  }
  const char*  dir = "/tmp/tindex";
  WriterCtx*   ctx = NULL;
  TFileHeader  header;
  TFileWriter* twrite = NULL;
};

static TFileValue* genTFileValue(const char* val) {
  TFileValue* tv = (TFileValue*)calloc(1, sizeof(TFileValue));
  int32_t     vlen = strlen(val) + 1;
  tv->colVal = (char*)calloc(1, vlen);
  memcpy(tv->colVal, val, vlen);

  tv->tableId = (SArray*)taosArrayInit(1, sizeof(uint64_t));
  for (size_t i = 0; i < 10; i++) {
    uint64_t v = i;
    taosArrayPush(tv->tableId, &v);
  }
  return tv;
}
static void destroyTFileValue(void* val) {
  TFileValue* tv = (TFileValue*)val;
  free(tv->colVal);
  taosArrayDestroy(tv->tableId);
  free(tv);
}

TEST_F(IndexTFileEnv, test_tfile_write) {
  TFileValue* v1 = genTFileValue("c");
  TFileValue* v2 = genTFileValue("a");

  SArray* data = (SArray*)taosArrayInit(4, sizeof(void*));

  taosArrayPush(data, &v1);
  taosArrayPush(data, &v2);

  tfileWriterPut(twrite, data);
  // tfileWriterDestroy(twrite);

  for (size_t i = 0; i < taosArrayGetSize(data); i++) {
    destroyTFileValue(taosArrayGetP(data, i));
  }
  taosArrayDestroy(data);
}