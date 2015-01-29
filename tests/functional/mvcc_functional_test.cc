/* -*- Mode: C++; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 *     Copyright 2010 Couchbase, Inc
 *
 *   Licensed under the Apache License, Version 2.0 (the "License");
 *   you may not use this file except in compliance with the License.
 *   You may obtain a copy of the License at
 *
 *       http://www.apache.org/licenses/LICENSE-2.0
 *
 *   Unless required by applicable law or agreed to in writing, software
 *   distributed under the License is distributed on an "AS IS" BASIS,
 *   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *   See the License for the specific language governing permissions and
 *   limitations under the License.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#if !defined(WIN32) && !defined(_WIN32)
#include <unistd.h>
#endif

#include "libforestdb/forestdb.h"
#include "test.h"

#include "internal_types.h"
#include "functional_util.h"

void multi_version_test()
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 2;
    fdb_file_handle *dbfile, *dbfile_new;
    fdb_kvs_handle *db;
    fdb_kvs_handle *db_new;
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_doc *rdoc;
    fdb_status status;

    char keybuf[256], metabuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 1048576;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;

    // open db
    fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc, (void *) "multi_version_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // insert documents
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // commit
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // open same db file using a new handle
    fdb_open(&dbfile_new, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile_new, &db_new, &kvs_config);
    status = fdb_set_log_callback(db_new, logCallbackFunc, (void *) "multi_version_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // update documents using the old handle
    for (i=0;i<n;++i){
        sprintf(metabuf, "meta2%d", i);
        sprintf(bodybuf, "body2%d", i);
        fdb_doc_update(&doc[i], (void*)metabuf, strlen(metabuf),
            (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // manually flush WAL and commit using the old handle
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // retrieve documents using the old handle
    for (i=0;i<n;++i){
        // search by key
        fdb_doc_create(&rdoc, doc[i]->key, doc[i]->keylen, NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);

        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);

        // free result document
        fdb_doc_free(rdoc);
    }

    // retrieve documents using the new handle
    for (i=0;i<n;++i){
        // search by key
        fdb_doc_create(&rdoc, doc[i]->key, doc[i]->keylen, NULL, 0, NULL, 0);
        status = fdb_get(db_new, rdoc);

        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);

        // free result document
        fdb_doc_free(rdoc);
    }

    // close and re-open the new handle
    fdb_kvs_close(db_new);
    fdb_close(dbfile_new);
    fdb_open(&dbfile_new, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile_new, &db_new, &kvs_config);
    status = fdb_set_log_callback(db_new, logCallbackFunc, (void *) "multi_version_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // retrieve documents using the new handle
    for (i=0;i<n;++i){
        // search by key
        fdb_doc_create(&rdoc, doc[i]->key, doc[i]->keylen, NULL, 0, NULL, 0);
        status = fdb_get(db_new, rdoc);

        TEST_CHK(status == FDB_RESULT_SUCCESS);
        // the new version of data should be read
        TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);

        // free result document
        fdb_doc_free(rdoc);
    }


    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    // close db file
    fdb_kvs_close(db);
    fdb_kvs_close(db_new);
    fdb_close(dbfile);
    fdb_close(dbfile_new);

    // free all resources
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("multi version test");
}

void crash_recovery_test()
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 10;
    fdb_file_handle *dbfile;
    fdb_kvs_handle *db;
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_doc *rdoc;
    fdb_status status;

    char keybuf[256], metabuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 0;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;

    // reopen db
    fdb_open(&dbfile, "./dummy2", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "crash_recovery_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // insert documents
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // commit
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // close the db
    fdb_kvs_close(db);
    fdb_close(dbfile);

    // Shutdown forest db in the middle of the test to simulate crash
    fdb_shutdown();

    // Now append garbage at the end of the file for a few blocks
    r = system(
       "dd if=/dev/zero bs=4096 of=./dummy2 oseek=3 count=2 >> errorlog.txt");
    (void)r;
    // Write 1024 bytes of non-block aligned garbage to end of file
    r = system(
       "dd if=/dev/zero bs=1024 of=./dummy2 oseek=20 count=1 >> errorlog.txt");
    (void)r;


    // reopen the same file
    fdb_open(&dbfile, "./dummy2", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "crash_recovery_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // retrieve documents
    for (i=0;i<n;++i){
        // search by key
        fdb_doc_create(&rdoc, doc[i]->key, doc[i]->keylen, NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);

        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);

        // free result document
        fdb_doc_free(rdoc);
    }

    // retrieve documents by sequence number
    for (i=0;i<n;++i){
        // search by seq
        fdb_doc_create(&rdoc, NULL, 0, NULL, 0, NULL, 0);
        rdoc->seqnum = i + 1;
        status = fdb_get_byseq(db, rdoc);

        TEST_CHK(status == FDB_RESULT_SUCCESS);

        // free result document
        fdb_doc_free(rdoc);
    }

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    // close db file
    fdb_kvs_close(db);
    fdb_close(dbfile);

    // free all resources
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("crash recovery test");
}

void snapshot_test()
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 20;
    int count;
    fdb_file_handle *dbfile;
    fdb_kvs_handle *db;
    fdb_kvs_handle *snap_db;
    fdb_seqnum_t snap_seq;
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_doc *rdoc = NULL;
    fdb_kvs_info kvs_info;
    fdb_status status;
    fdb_iterator *iterator;

    char keybuf[256], metabuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 0;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    // open db
    status = fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // Create a snapshot from an empty database file
    status = fdb_snapshot_open(db, &snap_db, 0);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // check if snapshot's sequence number is zero.
    fdb_get_kvs_info(snap_db, &kvs_info);
    TEST_CHK(kvs_info.last_seqnum == 0);
    // create an iterator on the snapshot for full range
    fdb_iterator_init(snap_db, &iterator, NULL, 0, NULL, 0, FDB_ITR_NONE);
    // Iterator should not return any items.
    status = fdb_iterator_get(iterator, &rdoc);
    TEST_CHK(status == FDB_RESULT_ITERATOR_FAIL);
    fdb_iterator_close(iterator);
    fdb_kvs_close(snap_db);

    // ------- Setup test ----------------------------------
    // insert documents of 0-4
    for (i=0; i<n/4; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // commit with a manual WAL flush (these docs go into HB-trie)
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // insert documents from 4 - 8
    for (; i < n/2 - 1; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // We want to create:
    // |WALFlushHDR|Key-Value1|HDR|Key-Value2|SnapshotHDR|Key-Value1|HDR|
    // Insert doc 9 with a different value to test duplicate elimination..
    sprintf(keybuf, "key%d", i);
    sprintf(metabuf, "meta%d", i);
    sprintf(bodybuf, "Body%d", i);
    fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
    fdb_set(db, doc[i]);

    // commit again without a WAL flush (last doc goes into the AVL tree)
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // Insert doc 9 now again with expected value..
    *(char *)doc[i]->body = 'b';
    fdb_set(db, doc[i]);
    // commit again without a WAL flush (these documents go into the AVL trees)
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // TAKE SNAPSHOT: pick up sequence number of a commit without a WAL flush
    snap_seq = doc[i]->seqnum;

    // Now re-insert doc 9 as another duplicate (only newer sequence number)
    fdb_set(db, doc[i]);
    // commit again without a WAL flush (last doc goes into the AVL tree)
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // insert documents from 10-14 into HB-trie
    for (++i; i < (n/2 + n/4); i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }
    // manually flush WAL & commit
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "snapshot_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // ---------- Snapshot tests begin -----------------------
    // Attempt to take snapshot with out-of-range marker..
    status = fdb_snapshot_open(db, &snap_db, 999999);
    TEST_CHK(status == FDB_RESULT_NO_DB_INSTANCE);

    // Init Snapshot of open file with saved document seqnum as marker
    status = fdb_snapshot_open(db, &snap_db, snap_seq);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // check snapshot's sequence number
    fdb_get_kvs_info(snap_db, &kvs_info);
    TEST_CHK(kvs_info.last_seqnum == snap_seq);

    // insert documents from 15 - 19 on file into the WAL
    for (; i < n; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }
    // commit without a WAL flush (This WAL must not affect snapshot)
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // create an iterator on the snapshot for full range
    fdb_iterator_init(snap_db, &iterator, NULL, 0, NULL, 0, FDB_ITR_NONE);

    // repeat until fail
    i=0;
    count=0;
    do {
        status = fdb_iterator_get(iterator, &rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);

        TEST_CMP(rdoc->key, doc[i]->key, rdoc->keylen);
        TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);

        fdb_doc_free(rdoc);
        rdoc = NULL;
        i ++;
        count++;
    } while (fdb_iterator_next(iterator) != FDB_RESULT_ITERATOR_FAIL);

    TEST_CHK(count==n/2); // Only unique items from the first half

    fdb_iterator_close(iterator);

    // create a sequence iterator on the snapshot for full range
    fdb_iterator_sequence_init(snap_db, &iterator, 0, 0, FDB_ITR_NONE);

    // repeat until fail
    i=0;
    count=0;
    do {
        status = fdb_iterator_get(iterator, &rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);

        TEST_CMP(rdoc->key, doc[i]->key, rdoc->keylen);
        TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);

        fdb_doc_free(rdoc);
        rdoc = NULL;
        i ++;
        count++;
    } while (fdb_iterator_next(iterator) != FDB_RESULT_ITERATOR_FAIL);

    TEST_CHK(count==n/2); // Only unique items from the first half

    fdb_iterator_close(iterator);

    // close db handle
    fdb_kvs_close(db);
    // close snapshot handle
    fdb_kvs_close(snap_db);
    // close db file
    fdb_close(dbfile);

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    // 1. Open Database File
    status = fdb_open(&dbfile, "./dummy2", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 2. Open KV Store
    status = fdb_kvs_open(dbfile, &db, "kv2", &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 2. Create Key 'a' and Commit
    status = fdb_set_kv(db, (void *) "a", 1, (void *)"val-a", 5);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_commit(dbfile, FDB_COMMIT_NORMAL);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 3. Create Key 'b' and Commit
    status = fdb_set_kv(db, (void *)"b", 1, (void *)"val-b", 5);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_commit(dbfile, FDB_COMMIT_NORMAL);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 4. Create Key 'c' and Commit
    status = fdb_set_kv(db, (void *)"c", 1, (void *)"val-c", 5);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_commit(dbfile, FDB_COMMIT_NORMAL);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 5. Remember seqnum for opening snapshot
    status = fdb_get_kvs_info(db, &kvs_info);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    snap_seq = kvs_info.last_seqnum;

    // 6.  Create an iterator
    status = fdb_iterator_init(db, &iterator, (void *)"a", 1,
                               (void *)"c", 1, FDB_ITR_NONE);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 7. Do a seek
    status = fdb_iterator_seek(iterator, (void *)"b", 1, FDB_ITR_SEEK_HIGHER);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 8. Close the iterator
    status = fdb_iterator_close(iterator);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 9. Open a snapshot at the same point
    status = fdb_snapshot_open(db, &snap_db, snap_seq);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // That works, now attempt to do exact same sequence on a snapshot
    // The snapshot is opened at the exact same place that the original
    // database handle should be at (last seq 3)

    // 10.  Create an iterator on snapshot
    status = fdb_iterator_init(snap_db, &iterator, (void *)"a", 1,
                               (void *)"c", 1, FDB_ITR_NONE);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 11. Do a seek
    status = fdb_iterator_seek(iterator, (void *)"b", 1, FDB_ITR_SEEK_HIGHER);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 12. Close the iterator
    status = fdb_iterator_close(iterator);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // close db handle
    fdb_kvs_close(db);
    // close snapshot handle
    fdb_kvs_close(snap_db);
    // close db file
    fdb_close(dbfile);

    // free all resources
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("snapshot test");
}

void snapshot_stats_test()
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 20;
    fdb_file_handle *dbfile;
    fdb_kvs_handle *db;
    fdb_kvs_handle *snap_db;
    fdb_seqnum_t snap_seq;
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_kvs_info kvs_info;
    fdb_status status;

    char keybuf[256], metabuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 0;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    // open db
    status = fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // ------- Setup test ----------------------------------
    // insert documents
    for (i=0; i<n; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
        fdb_doc_free(doc[i]);
    }

    // commit with a manual WAL flush (these docs go into HB-trie)
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // retrieve the sequence number for a snapshot open
    status = fdb_get_kvs_info(db, &kvs_info);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    snap_seq = kvs_info.last_seqnum;

    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "snapshot_stats_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // Init Snapshot of open file with saved document seqnum as marker
    status = fdb_snapshot_open(db, &snap_db, snap_seq);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // check snapshot's sequence number
    fdb_get_kvs_info(snap_db, &kvs_info);
    TEST_CHK(kvs_info.last_seqnum == snap_seq);

    TEST_CHK(kvs_info.doc_count == n);
    TEST_CHK(kvs_info.file == dbfile);

    // close snapshot handle
    fdb_kvs_close(snap_db);

    // Test stats by creating an in-memory snapshot
    status = fdb_snapshot_open(db, &snap_db, FDB_SNAPSHOT_INMEM);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // check snapshot's sequence number
    fdb_get_kvs_info(snap_db, &kvs_info);
    TEST_CHK(kvs_info.last_seqnum == snap_seq);

    TEST_CHK(kvs_info.doc_count == n);
    TEST_CHK(kvs_info.file == dbfile);

    // close snapshot handle
    fdb_kvs_close(snap_db);

    // close db handle
    fdb_kvs_close(db);
    // close db file
    fdb_close(dbfile);

    // free all resources
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("snapshot stats test");
}

void in_memory_snapshot_test()
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 20;
    int count;
    fdb_file_handle *dbfile;
    fdb_kvs_handle *db;
    fdb_kvs_handle *snap_db;
    fdb_seqnum_t snap_seq;
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_doc *rdoc;
    fdb_kvs_info kvs_info;
    fdb_status status;
    fdb_iterator *iterator;

    char keybuf[256], metabuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 0;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    // open db
    status = fdb_open(&dbfile, "./dummy1", &fconfig);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_kvs_open_default(dbfile, &db, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // Create a snapshot from an empty database file
    status = fdb_snapshot_open(db, &snap_db, 0);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // check if snapshot's sequence number is zero.
    fdb_get_kvs_info(snap_db, &kvs_info);
    TEST_CHK(kvs_info.last_seqnum == 0);
    // create an iterator on the snapshot for full range
    fdb_iterator_init(snap_db, &iterator, NULL, 0, NULL, 0, FDB_ITR_NONE);
    // Iterator should not return any items.
    status = fdb_iterator_get(iterator, &rdoc);
    TEST_CHK(status == FDB_RESULT_ITERATOR_FAIL);
    fdb_iterator_close(iterator);
    fdb_kvs_close(snap_db);

    // ------- Setup test ----------------------------------
    // insert documents of 0-4
    for (i=0; i<n/4; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // commit with a manual WAL flush (these docs go into HB-trie)
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // insert documents from 5 - 8
    for (; i < n/2 - 1; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // We want to create:
    // |WALFlushHDR|Key-Value1|HDR|Key-Value2|SnapshotHDR|Key-Value1|HDR|
    // Insert doc 9 with a different value to test duplicate elimination..
    sprintf(keybuf, "key%d", i);
    sprintf(metabuf, "meta%d", i);
    sprintf(bodybuf, "Body%d", i);
    fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
    fdb_set(db, doc[i]);

    // commit again without a WAL flush (last doc goes into the AVL tree)
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // Insert doc 9 now again with expected value..
    *(char *)doc[i]->body = 'b';
    fdb_set(db, doc[i]);

    // TAKE SNAPSHOT: pick up sequence number of a commit without a WAL flush
    snap_seq = doc[i]->seqnum;

    // Creation of a snapshot with a sequence number that was taken WITHOUT a
    // commit must fail even if it from the latest document that was set...
    fdb_get_kvs_info(db, &kvs_info);
    status = fdb_snapshot_open(db, &snap_db, kvs_info.last_seqnum);
    TEST_CHK(status == FDB_RESULT_NO_DB_INSTANCE);

    // ---------- Snapshot tests begin -----------------------
    // Initialize an in-memory snapshot Without a Commit...
    // WAL items are not flushed...
    status = fdb_snapshot_open(db, &snap_db, FDB_SNAPSHOT_INMEM);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // commit again without a WAL flush (these documents go into the AVL trees)
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // Now re-insert doc 9 as another duplicate (only newer sequence number)
    fdb_set(db, doc[i]);

    // commit again without a WAL flush (last doc goes into the AVL tree)
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // insert documents from 10-14 into HB-trie
    for (++i; i < (n/2 + n/4); i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
                (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "in_memory_snapshot_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // check snapshot's sequence number
    fdb_get_kvs_info(snap_db, &kvs_info);
    TEST_CHK(kvs_info.last_seqnum == snap_seq);

    // insert documents from 15 - 19 on file into the WAL
    for (; i < n; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }
    // commit without a WAL flush (This WAL must not affect snapshot)
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // create an iterator on the snapshot for full range
    fdb_iterator_init(snap_db, &iterator, NULL, 0, NULL, 0, FDB_ITR_NONE);

    // repeat until fail
    i=0;
    count=0;
    do {
        status = fdb_iterator_get(iterator, &rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);

        TEST_CMP(rdoc->key, doc[i]->key, rdoc->keylen);
        TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);

        fdb_doc_free(rdoc);
        rdoc = NULL;
        i++;
        count++;
    } while (fdb_iterator_next(iterator) != FDB_RESULT_ITERATOR_FAIL);

    TEST_CHK(count==n/2); // Only unique items from the first half

    fdb_iterator_close(iterator);

    // close db handle
    fdb_kvs_close(db);
    // close snapshot handle
    fdb_kvs_close(snap_db);

    // close db file
    fdb_close(dbfile);

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    // free all resources
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("in-memory snapshot test");
}

void snapshot_clone_test()
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 20;
    int count;
    fdb_file_handle *dbfile;
    fdb_kvs_handle *db;
    fdb_kvs_handle *snap_db, *snap_inmem;
    fdb_kvs_handle *snap_db2, *snap_inmem2; // clones from stable & inmemory
    fdb_seqnum_t snap_seq;
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_doc *rdoc;
    fdb_kvs_info kvs_info;
    fdb_status status;
    fdb_iterator *iterator;

    char keybuf[256], metabuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 0;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    // open db
    status = fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // Create a snapshot from an empty database file
    status = fdb_snapshot_open(db, &snap_db, 0);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // check if snapshot's sequence number is zero.
    fdb_get_kvs_info(snap_db, &kvs_info);
    TEST_CHK(kvs_info.last_seqnum == 0);
    // create an iterator on the snapshot for full range
    fdb_iterator_init(snap_db, &iterator, NULL, 0, NULL, 0, FDB_ITR_NONE);
    // Iterator should not return any items.
    status = fdb_iterator_next(iterator);
    TEST_CHK(status == FDB_RESULT_ITERATOR_FAIL);
    fdb_iterator_close(iterator);
    fdb_kvs_close(snap_db);

    // ------- Setup test ----------------------------------
    // insert documents of 0-4
    for (i=0; i<n/4; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // commit with a manual WAL flush (these docs go into HB-trie)
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // insert documents from 4 - 8
    for (; i < n/2 - 1; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // We want to create:
    // |WALFlushHDR|Key-Value1|HDR|Key-Value2|SnapshotHDR|Key-Value1|HDR|
    // Insert doc 9 with a different value to test duplicate elimination..
    sprintf(keybuf, "key%d", i);
    sprintf(metabuf, "meta%d", i);
    sprintf(bodybuf, "Body%d", i);
    fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
    fdb_set(db, doc[i]);

    // commit again without a WAL flush (last doc goes into the AVL tree)
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // Insert doc 9 now again with expected value..
    *(char *)doc[i]->body = 'b';
    fdb_set(db, doc[i]);
    // commit again without a WAL flush (these documents go into the AVL trees)
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // TAKE SNAPSHOT: pick up sequence number of a commit without a WAL flush
    snap_seq = doc[i]->seqnum;

    // Initialize an in-memory snapshot Without a Commit...
    // WAL items are not flushed...
    status = fdb_snapshot_open(db, &snap_inmem, FDB_SNAPSHOT_INMEM);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // Now re-insert doc 9 as another duplicate (only newer sequence number)
    fdb_set(db, doc[i]);
    // commit again without a WAL flush (last doc goes into the AVL tree)
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // insert documents from 10-14 into HB-trie
    for (++i; i < (n/2 + n/4); i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }
    // manually flush WAL & commit
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "snapshot_clone_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // ---------- Snapshot tests begin -----------------------
    // Attempt to take snapshot with out-of-range marker..
    status = fdb_snapshot_open(db, &snap_db, 999999);
    TEST_CHK(status == FDB_RESULT_NO_DB_INSTANCE);

    // Init Snapshot of open file with saved document seqnum as marker
    status = fdb_snapshot_open(db, &snap_db, snap_seq);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // Clone a snapshot into another snapshot...
    status = fdb_snapshot_open(snap_db, &snap_db2, snap_seq);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // close snapshot handle
    status = fdb_kvs_close(snap_db);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // check snapshot's sequence number
    fdb_get_kvs_info(snap_db2, &kvs_info);
    TEST_CHK(kvs_info.last_seqnum == snap_seq);

    // insert documents from 15 - 19 on file into the WAL
    for (; i < n; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }
    // commit without a WAL flush (This WAL must not affect snapshot)
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // Clone the in-memory snapshot into another snapshot
    status = fdb_snapshot_open(snap_inmem, &snap_inmem2, FDB_SNAPSHOT_INMEM);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // Stable Snapshot Scan Tests..........
    // Retrieve metaonly by key from snapshot
    i = snap_seq + 1;
    fdb_doc_create(&rdoc, doc[i]->key, doc[i]->keylen, NULL, 0, NULL, 0);
    status = fdb_get_metaonly(snap_db2, rdoc);
    TEST_CHK(status == FDB_RESULT_KEY_NOT_FOUND);
    fdb_doc_free(rdoc);

    i = 5;
    fdb_doc_create(&rdoc, doc[i]->key, doc[i]->keylen, NULL, 0, NULL, 0);
    status = fdb_get_metaonly(snap_db2, rdoc);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    TEST_CMP(rdoc->key, doc[i]->key, doc[i]->keylen);
    TEST_CMP(rdoc->meta, doc[i]->meta, doc[i]->metalen);
    fdb_doc_free(rdoc);

    // Retrieve by seq from snapshot
    fdb_doc_create(&rdoc, NULL, 0, NULL, 0, NULL, 0);
    rdoc->seqnum = 6;
    status = fdb_get_byseq(snap_db2, rdoc);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    TEST_CMP(rdoc->key, doc[i]->key, rdoc->keylen);
    TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);
    fdb_doc_free(rdoc);
    rdoc = NULL;

    // create an iterator on the snapshot for full range
    fdb_iterator_init(snap_db2, &iterator, NULL, 0, NULL, 0, FDB_ITR_NONE);

    // repeat until fail
    i=0;
    count=0;
    do {
        status = fdb_iterator_get(iterator, &rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(rdoc->key, doc[i]->key, rdoc->keylen);
        TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);

        fdb_doc_free(rdoc);
        rdoc = NULL;
        i ++;
        count++;
    } while (fdb_iterator_next(iterator) == FDB_RESULT_SUCCESS);

    TEST_CHK(count==n/2); // Only unique items from the first half

    fdb_iterator_close(iterator);

    // create an iterator on the in-memory snapshot clone for full range
    fdb_iterator_init(snap_inmem2, &iterator, NULL, 0, NULL, 0, FDB_ITR_NONE);

    // repeat until fail
    i=0;
    count=0;
    do {
        status = fdb_iterator_get(iterator, &rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(rdoc->key, doc[i]->key, rdoc->keylen);
        TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);

        fdb_doc_free(rdoc);
        rdoc = NULL;
        i ++;
        count++;
    } while(fdb_iterator_next(iterator) == FDB_RESULT_SUCCESS);

    TEST_CHK(count==n/2); // Only unique items from the first half

    fdb_iterator_close(iterator);

    // close db handle
    fdb_kvs_close(db);
    // close the in-memory snapshot handle
    fdb_kvs_close(snap_inmem);
    // close the in-memory clone snapshot handle
    fdb_kvs_close(snap_inmem2);
    // close the clone snapshot handle
    fdb_kvs_close(snap_db2);
    // close db file
    fdb_close(dbfile);

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    // free all resources
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("snapshot clone test");
}

void snapshot_markers_in_file_test(bool multi_kv)
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 20;
    int num_kvs = 4; // keep this the same as number of fdb_commit() calls
    fdb_file_handle *dbfile;
    fdb_kvs_handle **db = alca(fdb_kvs_handle *, num_kvs);
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_status status;
    fdb_snapshot_info_t *markers;
    uint64_t num_markers;

    char keybuf[256], metabuf[256], bodybuf[256];
    char kv_name[8];

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 0;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;
    fconfig.multi_kv_instances = multi_kv;

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    // open db
    fdb_open(&dbfile, "./dummy1", &fconfig);
    if (multi_kv) {
        for (r = 0; r < num_kvs; ++r) {
            sprintf(kv_name, "kv%d", r);
            fdb_kvs_open(dbfile, &db[r], kv_name, &kvs_config);
        }
    } else {
        num_kvs = 1;
        fdb_kvs_open_default(dbfile, &db[0], &kvs_config);
    }

   // ------- Setup test ----------------------------------
   // insert documents of 0-4
    for (i=0; i<n/4; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        for (r = 0; r < num_kvs; ++r) {
            fdb_set(db[r], doc[i]);
        }
    }

    // commit with a manual WAL flush (these docs go into HB-trie)
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // insert documents from 5 - 9
    for (; i < n/2; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        for (r = 0; r < num_kvs; ++r) {
            fdb_set(db[r], doc[i]);
        }
    }

    // commit again without a WAL flush
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // insert documents from 10-14 into HB-trie
    for (; i < (n/2 + n/4); i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        for (r = 0; r < num_kvs; ++r) {
            fdb_set(db[r], doc[i]);
        }
    }
    // manually flush WAL & commit
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // insert documents from 15 - 19 on file into the WAL
    for (; i < n; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        for (r = 0; r < num_kvs; ++r) {
            fdb_set(db[r], doc[i]);
        }
    }
    // commit without a WAL flush
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    for (r = 0; r < num_kvs; ++r) {
        status = fdb_set_log_callback(db[r], logCallbackFunc,
                                      (void *) "snapshot_markers_in_file");
        TEST_CHK(status == FDB_RESULT_SUCCESS);
    }

    status = fdb_get_all_snap_markers(dbfile, &markers, &num_markers);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    if (!multi_kv) {
        TEST_CHK(num_markers == 4);
        for (r = 0; r < num_kvs; ++r) {
            TEST_CHK(markers[r].num_kvs_markers == 1);
            TEST_CHK(markers[r].kvs_markers[0].seqnum == (n - r*5));
        }
    } else {
        TEST_CHK(num_markers == 8);
        for (r = 0; r < num_kvs; ++r) {
            TEST_CHK(markers[r].num_kvs_markers == num_kvs);
            for (i = 0; i < num_kvs; ++i) {
                TEST_CHK(markers[r].kvs_markers[i].seqnum == (n - r*5));
                sprintf(kv_name, "kv%d", i);
                TEST_CMP(markers[r].kvs_markers[i].kv_store_name, kv_name, 3);
            }
        }
    }

    status = fdb_free_snap_markers(markers, num_markers);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // close db file
    fdb_close(dbfile);

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    // free all resources
    fdb_shutdown();

    memleak_end();

    sprintf(bodybuf, "snapshot markers in file test %s", multi_kv ?
                                                        "multiple kv mode:"
                                                      : "single kv mode:");
    TEST_RESULT(bodybuf);
}

void rollback_forward_seqnum()
{

    TEST_INIT();
    memleak_start();

    int r;
    int i, n=100;
    int rb1_seqnum, rb2_seqnum;
    char keybuf[256];
    char setop[3];
    fdb_file_handle *dbfile;
    fdb_iterator *it;
    fdb_kvs_handle *kv1, *mirror_kv1;
    fdb_kvs_info info;
    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_doc *rdoc = NULL;
    fdb_status status;
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;

    fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open(dbfile, &kv1, "kv1", &kvs_config);
    fdb_kvs_open(dbfile, &mirror_kv1, NULL, &kvs_config);


    // set n docs within both dbs
    for(i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            NULL, 0, NULL, 0);
        fdb_set(kv1, doc[i]);
        fdb_set_kv(mirror_kv1, keybuf, strlen(keybuf), setop, 3);
    }

    // commit and save seqnum1
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);
    fdb_get_kvs_info(kv1, &info);
    rb1_seqnum = info.last_seqnum;

    // delete all docs in kv1
    for(i=0;i<n;++i){
        fdb_del(kv1, doc[i]);
    }

    // commit and save seqnum2
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);
    fdb_get_kvs_info(kv1, &info);
    rb2_seqnum = info.last_seqnum;


    // sets again
    for(i=0;i<n;++i){
        doc[i]->deleted = false;
        fdb_set(kv1, doc[i]);
    }

    // commit
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // rollback to first seqnum
    status = fdb_rollback(&kv1, rb1_seqnum);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // rollback to second seqnum
    status = fdb_rollback(&kv1, rb2_seqnum);
    TEST_CHK(status == FDB_RESULT_NO_DB_INSTANCE);

    status = fdb_iterator_sequence_init(mirror_kv1, &it, 0, 0, FDB_ITR_NONE);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    do {
        status = fdb_iterator_get(it, &rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        status = fdb_get_metaonly_byseq(kv1, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CHK(rdoc->deleted == false);
        fdb_doc_free(rdoc);
        rdoc = NULL;
    } while(fdb_iterator_next(it) != FDB_RESULT_ITERATOR_FAIL);

    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }
    fdb_iterator_close(it);
    fdb_kvs_close(kv1);
    fdb_close(dbfile);
    fdb_shutdown();
    memleak_end();

    TEST_RESULT("rollback forward seqnum");
}

void rollback_test(bool multi_kv)
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 20;
    int count;
    fdb_file_handle *dbfile, *dbfile_txn;
    fdb_kvs_handle *db, *db_txn;
    fdb_seqnum_t rollback_seq;
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_doc *rdoc = NULL;
    fdb_kvs_info kvs_info;
    fdb_status status;
    fdb_iterator *iterator;

    char keybuf[256], metabuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 0;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;
    fconfig.multi_kv_instances = multi_kv;

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    // open db
    fdb_open(&dbfile, "./dummy1", &fconfig);
    if (multi_kv) {
        fdb_kvs_open_default(dbfile, &db, &kvs_config);
    } else {
        fdb_kvs_open(dbfile, &db, NULL, &kvs_config);
    }

   // ------- Setup test ----------------------------------
   // insert documents of 0-4
    for (i=0; i<n/4; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // commit with a manual WAL flush (these docs go into HB-trie)
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // insert documents from 4 - 9
    for (; i < n/2; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }

    // commit again without a WAL flush
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // ROLLBACK POINT: pick up sequence number of a commit without a WAL flush
    rollback_seq = doc[i-1]->seqnum;

    // insert documents from 10-14 into HB-trie
    for (; i < (n/2 + n/4); i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }
    // manually flush WAL & commit
    fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);

    // insert documents from 15 - 19 on file into the WAL
    for (; i < n; i++){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            (void*)metabuf, strlen(metabuf), (void*)bodybuf, strlen(bodybuf));
        fdb_set(db, doc[i]);
    }
    // commit without a WAL flush
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "rollback_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // ---------- Rollback tests begin -----------------------
    // We have DB file with 5 HB-trie docs, 5 unflushed WAL docs occuring twice
    // Attempt to rollback to out-of-range marker..
    status = fdb_rollback(&db, 999999);
    TEST_CHK(status == FDB_RESULT_NO_DB_INSTANCE);

    // Open another handle & begin transaction
    fdb_open(&dbfile_txn, "./dummy1", &fconfig);
    if (multi_kv) {
        fdb_kvs_open_default(dbfile_txn, &db_txn, &kvs_config);
    } else {
        fdb_kvs_open(dbfile_txn, &db_txn, NULL, &kvs_config);
    }
    fdb_begin_transaction(dbfile_txn, FDB_ISOLATION_READ_COMMITTED);
    // Attempt to rollback while the transaction is active
    status =  fdb_rollback(&db, rollback_seq);
    // Must fail
    TEST_CHK(status == FDB_RESULT_FAIL_BY_TRANSACTION);
    fdb_abort_transaction(dbfile_txn);
    fdb_kvs_close(db_txn);
    fdb_close(dbfile_txn);

    // Rollback to saved marker from above
    status = fdb_rollback(&db, rollback_seq);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // check handle's sequence number
    fdb_get_kvs_info(db, &kvs_info);
    TEST_CHK(kvs_info.last_seqnum == rollback_seq);

    // Modify an item and update into the rollbacked file..
    i = n/2;
    *(char *)doc[i]->body = 'B';
    fdb_set(db, doc[i]);
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // create an iterator on the rollback for full range
    fdb_iterator_sequence_init(db, &iterator, 0, 0, FDB_ITR_NONE);

    // repeat until fail
    i=0;
    count=0;
    do {
        status = fdb_iterator_get(iterator, &rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);

        TEST_CMP(rdoc->key, doc[i]->key, rdoc->keylen);
        TEST_CMP(rdoc->meta, doc[i]->meta, rdoc->metalen);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);

        fdb_doc_free(rdoc);
        rdoc = NULL;
        i ++;
        count++;
    } while (fdb_iterator_next(iterator) != FDB_RESULT_ITERATOR_FAIL);

    TEST_CHK(count==n/2 + 1); // Items from first half and newly set item

    fdb_iterator_close(iterator);

    // close db file
    fdb_kvs_close(db);
    fdb_close(dbfile);

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    // free all resources
    fdb_shutdown();

    memleak_end();

    sprintf(bodybuf, "rollback test %s", multi_kv ? "multiple kv mode:"
                                                  : "single kv mode:");
    TEST_RESULT(bodybuf);
}



void rollback_and_snapshot_test()
{
    TEST_INIT();

    memleak_start();

    fdb_seqnum_t seqnum, rollback_seqnum;
    fdb_kvs_info kvs_info;
    fdb_status status;
    fdb_config config;
    fdb_kvs_config kvs_config;
    fdb_file_handle *dbfile;
    fdb_kvs_handle *db,  *snapshot;
    int r;

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    // MB-12530 open db
    config = fdb_get_default_config();
    kvs_config = fdb_get_default_kvs_config();
    status = fdb_open(&dbfile, "dummy", &config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_kvs_open(dbfile, &db, NULL, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 2. Create Key 'a' and Commit
    status = fdb_set_kv(db, (void *) "a", 1, (void *)"val-a", 5);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_commit(dbfile, FDB_COMMIT_NORMAL);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    status = fdb_get_kvs_info(db, &kvs_info);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 3. Create Key 'b' and Commit
    status = fdb_set_kv(db, (void *)"b", 1, (void *)"val-b", 5);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_commit(dbfile, FDB_COMMIT_NORMAL);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    status = fdb_get_kvs_info(db, &kvs_info);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    seqnum = kvs_info.last_seqnum;

    // 4.  Remember this as our rollback point
    rollback_seqnum = seqnum;

    // 5. Create Key 'c' and Commit
    status = fdb_set_kv(db, (void *)"c", 1,(void *) "val-c", 5);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_commit(dbfile, FDB_COMMIT_NORMAL);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    status = fdb_get_kvs_info(db, &kvs_info);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 6. Rollback to rollback point (seq 2)
    status = fdb_rollback(&db, rollback_seqnum);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    status = fdb_get_kvs_info(db, &kvs_info);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    seqnum = kvs_info.last_seqnum;

    // 7. Verify that Key 'c' is not found
    void *val;
    size_t vallen;
    status = fdb_get_kv(db, (void *)"c", 1, &val, &vallen);
    TEST_CHK(status == FDB_RESULT_KEY_NOT_FOUND);

    // 8. Open a snapshot at the same point
    status = fdb_snapshot_open(db, &snapshot, seqnum);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // 9. Verify that Key 'c' is not found
    status = fdb_get_kv(snapshot, (void *)"c", 1, &val, &vallen);
    TEST_CHK(status == FDB_RESULT_KEY_NOT_FOUND);

    // close the snapshot db
    fdb_kvs_close(snapshot);

    // close db file
    fdb_close(dbfile);

    // free all resources
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("rollback and snapshot test");
}

void rollback_ncommits()
{

    TEST_INIT();
    memleak_start();

    int r;
    int i, j, n=100;
    int ncommits=10;
    char keybuf[256];
    fdb_file_handle *dbfile;
    fdb_kvs_handle *kv1, *kv2;
    fdb_kvs_info info;
    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fdb_status status;
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 0;

    fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open(dbfile, &kv1, "kv1", &kvs_config);
    fdb_kvs_open(dbfile, &kv2, NULL, &kvs_config);


    for(j=0;j<ncommits;++j){

        // set n docs per commit
        for(i=0;i<n;++i){
            sprintf(keybuf, "key%02d%03d", j, i);
            fdb_set_kv(kv1, keybuf, strlen(keybuf), NULL, 0);
            fdb_set_kv(kv2, keybuf, strlen(keybuf), NULL, 0);
        }
        // alternate commit pattern
        if((j % 2) == 0){
            fdb_commit(dbfile, FDB_COMMIT_NORMAL);
        } else {
            fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);
        }

        // doc_count should match seqnum since they are unique
        fdb_get_kvs_info(kv1, &info);
        TEST_CHK(info.doc_count == info.last_seqnum);
    }

    // iteratively rollback 5 commits
     for(j=ncommits;j>0;--j){
        status = fdb_rollback(&kv1, j*n);
        TEST_CHK(status == FDB_RESULT_SUCCESS);

        // check rollback doc_count
        fdb_get_kvs_info(kv1, &info);
        TEST_CHK(info.doc_count == info.last_seqnum);
    }

    fdb_kvs_close(kv1);
    fdb_close(dbfile);
    fdb_shutdown();
    memleak_end();

    TEST_RESULT("rollback n commits");
}

void transaction_test()
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 10;
    fdb_file_handle *dbfile, *dbfile_txn1, *dbfile_txn2, *dbfile_txn3;
    fdb_kvs_handle *db, *db_txn1, *db_txn2, *db_txn3;
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_doc *rdoc;
    fdb_status status;

    char keybuf[256], metabuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 0;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.purging_interval = 0;
    fconfig.compaction_threshold = 0;

    // open db
    fdb_open(&dbfile, "dummy1", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "transaction_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // open db and begin transactions
    fdb_open(&dbfile_txn1, "dummy1", &fconfig);
    fdb_open(&dbfile_txn2, "dummy1", &fconfig);
    fdb_kvs_open_default(dbfile_txn1, &db_txn1, &kvs_config);
    fdb_kvs_open_default(dbfile_txn2, &db_txn2, &kvs_config);
    fdb_begin_transaction(dbfile_txn1, FDB_ISOLATION_READ_COMMITTED);
    fdb_begin_transaction(dbfile_txn2, FDB_ISOLATION_READ_COMMITTED);

    // insert half docs into txn1
    for (i=0;i<n/2;++i){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
                                (void*)metabuf, strlen(metabuf),
                                (void*)bodybuf, strlen(bodybuf));
        fdb_set(db_txn1, doc[i]);
    }

    // insert other half docs into txn2
    for (i=n/2;i<n;++i){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
                                (void*)metabuf, strlen(metabuf),
                                (void*)bodybuf, strlen(bodybuf));
        fdb_set(db_txn2, doc[i]);
    }

    // uncommitted docs should not be read by the other transaction that
    // doesn't allow uncommitted reads.
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db_txn1, rdoc);
        if (i<n/2) {
            TEST_CHK(status == FDB_RESULT_SUCCESS);
        } else {
            TEST_CHK(status != FDB_RESULT_SUCCESS);
        }
        fdb_doc_free(rdoc);
    }

    // uncommitted docs can be read by the transaction that allows uncommitted reads.
    fdb_open(&dbfile_txn3, "dummy1", &fconfig);
    fdb_kvs_open_default(dbfile_txn3, &db_txn3, &kvs_config);
    fdb_begin_transaction(dbfile_txn3, FDB_ISOLATION_READ_UNCOMMITTED);
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db_txn3, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        fdb_doc_free(rdoc);
    }
    fdb_end_transaction(dbfile_txn3, FDB_COMMIT_NORMAL);
    fdb_close(dbfile_txn3);

    // commit and end txn1
    fdb_end_transaction(dbfile_txn1, FDB_COMMIT_NORMAL);

    // uncommitted docs should not be read generally
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);
        if (i<n/2) {
            TEST_CHK(status == FDB_RESULT_SUCCESS);
        } else {
            TEST_CHK(status != FDB_RESULT_SUCCESS);
        }
        fdb_doc_free(rdoc);
    }

    // read uncommitted docs using the same transaction
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db_txn2, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        fdb_doc_free(rdoc);
    }

    // abort txn2
    fdb_abort_transaction(dbfile_txn2);

    // close & re-open db file
    fdb_close(dbfile);
    fdb_open(&dbfile, "dummy1", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "transaction_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // uncommitted docs should not be read after reopening the file either
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);
        if (i<n/2) {
            TEST_CHK(status == FDB_RESULT_SUCCESS);
        } else {
            TEST_CHK(status != FDB_RESULT_SUCCESS);
        }
        fdb_doc_free(rdoc);
    }

    // insert other half docs & commit
    for (i=n/2;i<n;++i){
        fdb_set(db, doc[i]);
    }
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // now all docs can be read generally
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        fdb_doc_free(rdoc);
    }

    // begin transactions
    fdb_begin_transaction(dbfile_txn1, FDB_ISOLATION_READ_COMMITTED);
    fdb_begin_transaction(dbfile_txn2, FDB_ISOLATION_READ_COMMITTED);

    // concurrently update docs
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        sprintf(metabuf, "meta%d", i);
        sprintf(bodybuf, "body%d_txn1", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf),
                                (void*)metabuf, strlen(metabuf),
                                (void*)bodybuf, strlen(bodybuf));
        fdb_set(db_txn1, rdoc);
        fdb_doc_free(rdoc);

        sprintf(bodybuf, "body%d_txn2", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf),
                                (void*)metabuf, strlen(metabuf),
                                (void*)bodybuf, strlen(bodybuf));
        fdb_set(db_txn2, rdoc);
        fdb_doc_free(rdoc);
    }

    // retrieve check
    for (i=0;i<n;++i){
        // general retrieval
        sprintf(keybuf, "key%d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(rdoc->body, doc[i]->body, rdoc->bodylen);
        fdb_doc_free(rdoc);

        // get from txn1
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db_txn1, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        sprintf(bodybuf, "body%d_txn1", i);
        TEST_CMP(rdoc->body, bodybuf, rdoc->bodylen);
        fdb_doc_free(rdoc);

        // get from txn2
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db_txn2, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        sprintf(bodybuf, "body%d_txn2", i);
        TEST_CMP(rdoc->body, bodybuf, rdoc->bodylen);
        fdb_doc_free(rdoc);
    }

    // commit txn2 & retrieve check
    fdb_end_transaction(dbfile_txn2, FDB_COMMIT_NORMAL);
    for (i=0;i<n;++i){
        // general retrieval
        sprintf(keybuf, "key%d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        sprintf(bodybuf, "body%d_txn2", i);
        TEST_CMP(rdoc->body, bodybuf, rdoc->bodylen);
        fdb_doc_free(rdoc);

        // get from txn1
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db_txn1, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        sprintf(bodybuf, "body%d_txn1", i);
        TEST_CMP(rdoc->body, bodybuf, rdoc->bodylen);
        fdb_doc_free(rdoc);
    }

    // commit txn1 & retrieve check
    fdb_end_transaction(dbfile_txn1, FDB_COMMIT_NORMAL);
    for (i=0;i<n;++i){
        // general retrieval
        sprintf(keybuf, "key%d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        sprintf(bodybuf, "body%d_txn1", i);
        TEST_CMP(rdoc->body, bodybuf, rdoc->bodylen);
        fdb_doc_free(rdoc);
    }

    // begin new transaction
    fdb_begin_transaction(dbfile_txn1, FDB_ISOLATION_READ_COMMITTED);
    // update doc#5
    i = 5;
    sprintf(keybuf, "key%d", i);
    sprintf(metabuf, "meta%d", i);
    sprintf(bodybuf, "body%d_before_compaction", i);
    fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf),
                            (void*)metabuf, strlen(metabuf),
                            (void*)bodybuf, strlen(bodybuf));
    fdb_set(db_txn1, rdoc);
    fdb_doc_free(rdoc);

    // do compaction
    fdb_compact(dbfile, "dummy2");

    // retrieve doc#5
    // using txn1
    fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
    status = fdb_get(db_txn1, rdoc);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    TEST_CMP(rdoc->body, bodybuf, rdoc->bodylen);
    fdb_doc_free(rdoc);

    // general retrieval
    fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
    status = fdb_get(db, rdoc);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    sprintf(bodybuf, "body%d_txn1", i);
    TEST_CMP(rdoc->body, bodybuf, rdoc->bodylen);
    fdb_doc_free(rdoc);

    // commit transaction
    fdb_end_transaction(dbfile_txn1, FDB_COMMIT_NORMAL);
    // retrieve check
    for (i=0;i<n;++i){
        // general get
        sprintf(keybuf, "key%d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        if (i != 5) {
            sprintf(bodybuf, "body%d_txn1", i);
        } else {
            sprintf(bodybuf, "body%d_before_compaction", i);
        }
        TEST_CMP(rdoc->body, bodybuf, rdoc->bodylen);
        fdb_doc_free(rdoc);
    }

    // close & re-open db file
    fdb_close(dbfile);
    fdb_open(&dbfile, "dummy2", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "transaction_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    // retrieve check again
    for (i=0;i<n;++i){
        // general get
        sprintf(keybuf, "key%d", i);
        fdb_doc_create(&rdoc, (void*)keybuf, strlen(keybuf), NULL, 0, NULL, 0);
        status = fdb_get(db, rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        if (i != 5) {
            sprintf(bodybuf, "body%d_txn1", i);
        } else {
            sprintf(bodybuf, "body%d_before_compaction", i);
        }
        TEST_CMP(rdoc->body, bodybuf, rdoc->bodylen);
        fdb_doc_free(rdoc);
    }

    // close db file
    fdb_close(dbfile);
    fdb_close(dbfile_txn1);
    fdb_close(dbfile_txn2);

    // free all documents
    for (i=0;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    // free all resources
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("transaction test");
}

void transaction_simple_api_test()
{
    TEST_INIT();

    memleak_start();

    int i, r;
    int n = 10;
    size_t valuelen;
    void *value;
    fdb_file_handle *dbfile, *dbfile_txn1, *dbfile_txn2;
    fdb_kvs_handle *db, *db_txn1, *db_txn2;
    fdb_status status;

    char keybuf[256], bodybuf[256];

    // remove previous dummy files
    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fconfig.buffercache_size = 0;
    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.purging_interval = 0;
    fconfig.compaction_threshold = 0;

    // open db
    fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile, &db, &kvs_config);
    status = fdb_set_log_callback(db, logCallbackFunc,
                                  (void *) "transaction_simple_api_test");
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // insert key-value pairs
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        sprintf(bodybuf, "body%d", i);
        status = fdb_set_kv(db, keybuf, strlen(keybuf), bodybuf, strlen(bodybuf));
        TEST_CHK(status == FDB_RESULT_SUCCESS);
    }

    // commit
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // open db and begin transactions
    fdb_open(&dbfile_txn1, "./dummy1", &fconfig);
    fdb_open(&dbfile_txn2, "./dummy1", &fconfig);
    fdb_kvs_open_default(dbfile_txn1, &db_txn1, &kvs_config);
    fdb_kvs_open_default(dbfile_txn2, &db_txn2, &kvs_config);
    fdb_begin_transaction(dbfile_txn1, FDB_ISOLATION_READ_COMMITTED);
    fdb_begin_transaction(dbfile_txn2, FDB_ISOLATION_READ_COMMITTED);

    // concurrently update docs
    for (i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        sprintf(bodybuf, "body%d_txn1", i);
        fdb_set_kv(db_txn1, keybuf, strlen(keybuf), bodybuf, strlen(bodybuf));

        sprintf(keybuf, "key%d", i);
        sprintf(bodybuf, "body%d_txn2", i);
        fdb_set_kv(db_txn2, keybuf, strlen(keybuf), bodybuf, strlen(bodybuf));
    }

    // retrieve key-value pairs
    for (i=0;i<n;++i){
        // general retrieval
        sprintf(keybuf, "key%d", i);
        sprintf(bodybuf, "body%d", i);
        status = fdb_get_kv(db, keybuf, strlen(keybuf), &value, &valuelen);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(value, bodybuf, valuelen);
        free(value);

        // txn1
        sprintf(keybuf, "key%d", i);
        sprintf(bodybuf, "body%d_txn1", i);
        status = fdb_get_kv(db_txn1, keybuf, strlen(keybuf), &value, &valuelen);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(value, bodybuf, valuelen);
        free(value);

        // txn2
        sprintf(keybuf, "key%d", i);
        sprintf(bodybuf, "body%d_txn2", i);
        status = fdb_get_kv(db_txn2, keybuf, strlen(keybuf), &value, &valuelen);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(value, bodybuf, valuelen);
        free(value);
    }

    // commit txn1
    fdb_end_transaction(dbfile_txn1, FDB_COMMIT_NORMAL);
    for (i=0;i<n;++i){
        // general retrieval
        sprintf(keybuf, "key%d", i);
        sprintf(bodybuf, "body%d_txn1", i);
        status = fdb_get_kv(db, keybuf, strlen(keybuf), &value, &valuelen);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(value, bodybuf, valuelen);
        free(value);

        // txn2
        sprintf(keybuf, "key%d", i);
        sprintf(bodybuf, "body%d_txn2", i);
        status = fdb_get_kv(db_txn2, keybuf, strlen(keybuf), &value, &valuelen);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(value, bodybuf, valuelen);
        free(value);
    }

    // commit txn2
    fdb_end_transaction(dbfile_txn2, FDB_COMMIT_NORMAL);
    for (i=0;i<n;++i){
        // general retrieval
        sprintf(keybuf, "key%d", i);
        sprintf(bodybuf, "body%d_txn2", i);
        status = fdb_get_kv(db, keybuf, strlen(keybuf), &value, &valuelen);
        TEST_CHK(status == FDB_RESULT_SUCCESS);
        TEST_CMP(value, bodybuf, valuelen);
        free(value);
    }

    // close db file
    fdb_close(dbfile);
    fdb_close(dbfile_txn1);
    fdb_close(dbfile_txn2);

    // free all resources
    fdb_shutdown();

    memleak_end();

    TEST_RESULT("transaction simple API test");
}


void rollback_prior_to_ops(bool walflush)
{

    TEST_INIT();
    memleak_start();

    int r;
    int i, j, n=100;
    int expected_doc_count = 0;
    int rb_seqnum;
    char keybuf[256], bodybuf[256];
    fdb_file_handle *dbfile;
    fdb_iterator *it;
    fdb_kvs_handle *kv1, *mirror_kv1;
    fdb_kvs_info info;
    fdb_config fconfig = fdb_get_default_config();
    fdb_kvs_config kvs_config = fdb_get_default_kvs_config();
    fdb_doc **doc = alca(fdb_doc*, n);
    fdb_doc *rdoc = NULL, *vdoc;
    fdb_status status;

    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    fconfig.wal_threshold = 1024;
    fconfig.flags = FDB_OPEN_FLAG_CREATE;
    fconfig.compaction_threshold = 10;

    fdb_open(&dbfile, "./dummy1", &fconfig);
    fdb_kvs_open(dbfile, &kv1, "kv1", &kvs_config);

    if(walflush){
        fdb_kvs_open(dbfile, &mirror_kv1, NULL, &kvs_config);
    } else {
        fdb_kvs_open(dbfile, &mirror_kv1, "mirror", &kvs_config);
    }

    // set n docs
    for(i=0;i<n;++i){
        sprintf(keybuf, "key%d", i);
        fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
            NULL, 0, NULL, 0);
        fdb_set(kv1, doc[i]);
        fdb_set(mirror_kv1, doc[i]);
        expected_doc_count++;
    }

    // commit
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);

    // delete subset of recently loaded docs
    for(i=0;i<n/2;++i){
        fdb_del(kv1, doc[i]);
        fdb_del(mirror_kv1, doc[i]);
        expected_doc_count--;
        fdb_doc_free(doc[i]);
    }

    for(i=n/2;i<n;++i){
        fdb_doc_free(doc[i]);
    }

    // commit and save seqnum
    fdb_commit(dbfile, FDB_COMMIT_NORMAL);
    fdb_get_kvs_info(kv1, &info);
    TEST_CHK(info.doc_count == expected_doc_count);
    rb_seqnum = info.last_seqnum;

    for (j=0;j<100;++j){
        // load some more docs but stop mirroring
        for(i=0;i<n;++i){
            sprintf(keybuf, "key%d", n+i);
            fdb_doc_create(&doc[i], (void*)keybuf, strlen(keybuf),
                NULL, 0, NULL, 0);
            fdb_set(kv1, doc[i]);
            if( n%2 == 0){
                fdb_del(kv1, doc[i]);
            }
            fdb_doc_free(doc[i]);
        }
    }

    // commit wal or normal
    if(walflush){
        fdb_commit(dbfile, FDB_COMMIT_MANUAL_WAL_FLUSH);
    } else {
        fdb_commit(dbfile, FDB_COMMIT_NORMAL);
    }

    // rollback
    status = fdb_rollback(&kv1, rb_seqnum);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    fdb_get_kvs_info(kv1, &info);
    TEST_CHK(info.doc_count == expected_doc_count);
    fdb_get_kvs_info(mirror_kv1, &info);
    TEST_CHK(info.doc_count == expected_doc_count);

    status = fdb_iterator_sequence_init(mirror_kv1, &it, 0, 0, FDB_ITR_NONE);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    do {
        status = fdb_iterator_get_metaonly(it, &rdoc);
        TEST_CHK(status == FDB_RESULT_SUCCESS);

        fdb_doc_create(&vdoc, rdoc->key, rdoc->keylen,
                              rdoc->meta, rdoc->metalen,
                              rdoc->body, rdoc->bodylen);
        status = fdb_get(kv1, vdoc);
        TEST_CHK(rdoc->deleted == vdoc->deleted);
        if (rdoc->deleted){
            TEST_CHK(status == FDB_RESULT_KEY_NOT_FOUND);
        } else {
            TEST_CHK(status == FDB_RESULT_SUCCESS);
        }
        fdb_doc_free(rdoc);
        rdoc = NULL;
        fdb_doc_free(vdoc);
    } while (fdb_iterator_next(it) != FDB_RESULT_ITERATOR_FAIL);

    fdb_iterator_close(it);
    fdb_kvs_close(mirror_kv1);
    fdb_kvs_close(kv1);
    fdb_close(dbfile);
    fdb_shutdown();
    memleak_end();

    sprintf(bodybuf, "rollback prior to ops test %s",
            walflush ? "commit with wal flush:" : "normal commit:");
    TEST_RESULT(bodybuf);
}

void auto_compaction_snapshots_test()
{
    TEST_INIT();

    memleak_start();

    fdb_file_handle *file;
    fdb_kvs_handle *kvs, *snapshot;
    fdb_status status;
    fdb_config config;
    fdb_kvs_config kvs_config;
    fdb_seqnum_t seqnum;
    fdb_kvs_info info;
    fdb_doc *rdoc;

    int i, r;

    r = system(SHELL_DEL" dummy* > errorlog.txt");
    (void)r;

    // Open Database File
    config = fdb_get_default_config();
    config.compaction_mode=FDB_COMPACTION_AUTO;
    config.compactor_sleep_duration=1;
    status = fdb_open(&file, "dummy1", &config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // Open KV Store
    kvs_config = fdb_get_default_kvs_config();
    status = fdb_kvs_open_default(file, &kvs, &kvs_config);
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    // Several kv pairs
    for(i=0;i<100000;i++) {
        char str[15];
        sprintf(str, "%d", i);
        status = fdb_set_kv(kvs, str, strlen(str), (void*)"value", 5);
        TEST_CHK(status == FDB_RESULT_SUCCESS);

        // Every 10 Commit
        if (i % 10 == 0) {
            status = fdb_commit(file, FDB_COMMIT_NORMAL);
            TEST_CHK(status == FDB_RESULT_SUCCESS);
        }

        // Every 100 iterations Get Seq/Snapshot
        if (i % 100 == 0) {
            status = fdb_get_kvs_info(kvs, &info);
            TEST_CHK(status == FDB_RESULT_SUCCESS);
            seqnum = info.last_seqnum;
            // Open durable snapshot in the midst of compaction..
            status = fdb_snapshot_open(kvs, &snapshot, seqnum);
            TEST_CHK(status == FDB_RESULT_SUCCESS);
            // verify last doc set is captured in snapshot..
            fdb_doc_create(&rdoc, NULL, 0, NULL, 0, NULL, 0);
            rdoc->seqnum = seqnum;
            status = fdb_get_byseq(kvs, rdoc);
            TEST_CHK(status == FDB_RESULT_SUCCESS);
            TEST_CMP(rdoc->key, str, strlen(str));
            // free result document
            fdb_doc_free(rdoc);
            status = fdb_kvs_close(snapshot);
            TEST_CHK(status == FDB_RESULT_SUCCESS);

            // Open in-memory snapshot and verify content
            status = fdb_snapshot_open(kvs, &snapshot, FDB_SNAPSHOT_INMEM);
            TEST_CHK(status == FDB_RESULT_SUCCESS);
            // verify last doc set is captured in snapshot..
            fdb_doc_create(&rdoc, NULL, 0, NULL, 0, NULL, 0);
            rdoc->seqnum = seqnum;
            status = fdb_get_byseq(kvs, rdoc);
            TEST_CHK(status == FDB_RESULT_SUCCESS);
            TEST_CMP(rdoc->key, str, strlen(str));
            // free result document
            fdb_doc_free(rdoc);
            status = fdb_kvs_close(snapshot);
            TEST_CHK(status == FDB_RESULT_SUCCESS);
        }
    }

    status = fdb_close(file);
    TEST_CHK(status == FDB_RESULT_SUCCESS);
    status = fdb_shutdown();
    TEST_CHK(status == FDB_RESULT_SUCCESS);

    memleak_end();

    TEST_RESULT("auto compaction with snapshots test");
}

int main(){

    multi_version_test();
#ifdef __CRC32
    crash_recovery_test();
#endif
    snapshot_test();
    in_memory_snapshot_test();
    snapshot_clone_test();
    snapshot_stats_test();
    snapshot_markers_in_file_test(true); // multi kv instance mode
    snapshot_markers_in_file_test(false); // single kv instance mode
    rollback_forward_seqnum();
    rollback_test(false); // single kv instance mode
    rollback_test(true); // multi kv instance mode
    rollback_and_snapshot_test();
    rollback_ncommits();
    transaction_test();
    transaction_simple_api_test();
    rollback_prior_to_ops(true); // wal commit
    rollback_prior_to_ops(false); // normal commit
    auto_compaction_snapshots_test(); // test snapshots with auto-compaction

    return 0;
}
