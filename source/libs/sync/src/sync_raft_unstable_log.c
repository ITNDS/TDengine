/*
 * Copyright (c) 2019 TAOS Data, Inc. <cli@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "syncInt.h"
#include "sync_raft_entry.h"
#include "sync_raft_log.h"
#include "sync_raft_proto.h"
#include "sync_raft_unstable_log.h"

static void mustCheckOutOfBounds(const SSyncRaftUnstableLog* unstable, SyncIndex lo, SyncIndex hi);

// unstable.entries[i] has raft log position i+unstable.offset.
// Note that unstable.offset may be less than the highest log
// position in storage; this means that the next write to storage
// might need to truncate the log before persisting unstable.entries.
struct SSyncRaftUnstableLog {
  SSyncRaft* pRaft;

  SyncRaftSnapshot* snapshot;

  // all entries that have not yet been written to storage.
  SSyncRaftEntryArray* entries;

  SyncIndex offset;
};

SSyncRaftUnstableLog* syncRaftCreateUnstableLog(SSyncRaft* pRaft, SyncIndex lastIndex) {
  SSyncRaftUnstableLog* unstable = (SSyncRaftUnstableLog*)malloc(sizeof(SSyncRaftUnstableLog));
  if (unstable == NULL) {
    return NULL;
  }

  unstable->entries = syncRaftCreateEntryArray();
  if (unstable->entries == NULL) {
    free(unstable);
    return NULL;
  }
  unstable->offset = lastIndex + 1;
  unstable->pRaft = pRaft;

  return unstable;
}

// maybeFirstIndex returns the index of the first possible entry in entries
// if it has a snapshot.
bool syncRaftUnstableLogMaybeFirstIndex(const SSyncRaftUnstableLog* unstable, SyncIndex* index) {
  if (unstable->snapshot != NULL) {
    *index = unstable->snapshot->meta.index + 1;
    return true;
  }

  *index = 0;
  return false;
}

// maybeLastIndex returns the last index if it has at least one
// unstable entry or snapshot.
bool syncRaftUnstableLogMaybeLastIndex(const SSyncRaftUnstableLog* unstable, SyncIndex* index) {
  int num = syncRaftNumOfEntries(unstable->entries);
  if (num > 0) {
    *index = unstable->offset + num + 1;
    return true;
  }
  if (unstable->snapshot != NULL) {
    *index = unstable->snapshot->meta.index + 1;
    return true;
  }

  *index = 0;
  return false;
}

// maybeTerm returns the term of the entry at index i, if there
// is any.
bool syncRaftUnstableLogMaybeTerm(const SSyncRaftUnstableLog* unstable, SyncIndex i, SyncTerm* term) {
  *term = SYNC_NON_TERM;

  if (i < unstable->offset) {
    if (unstable->snapshot != NULL && unstable->snapshot->meta.index == i) {
      *term = unstable->snapshot->meta.term;
      return true;
    }

    return false;
  }
  
  SyncIndex lastIndex;
  bool ok = syncRaftUnstableLogMaybeLastIndex(unstable, &lastIndex);
  if (!ok) {
    return false;
  }
  if (i > lastIndex) {
    return false;
  }

  *term = syncRaftTermOfPosition(unstable->entries, i - unstable->offset);
  return true;
}

void syncRaftUnstableLogStableTo(SSyncRaftUnstableLog* unstable, SyncIndex i, SyncTerm term) {
  SyncTerm gt;
  bool ok = syncRaftUnstableLogMaybeTerm(unstable, i, &gt);
  if (!ok) {
    return;
  }

	// if i < offset, term is matched with the snapshot
	// only update the unstable entries if term is matched with
	// an unstable entry.
  if (gt == term && i >= unstable->offset) {
    syncRaftRemoveEntriesBeforePosition(unstable->entries, i - unstable->offset);
    unstable->offset += 1;
  }
}

int syncRaftUnstableLogTruncateAndAppend(SSyncRaftUnstableLog* unstable, const SSyncRaftEntry* entries, int n) {
  SyncIndex afterIndex = entries[0].index;
  int num = syncRaftNumOfEntries(unstable->entries);
  int ret, i;
  SSyncRaft* pRaft = unstable->pRaft;
  SSyncLogStore* pLogStore = &(pRaft->logStore);

  if (afterIndex == unstable->offset + num) {
		// after is the next index in the u.entries
		// directly append
    ret = syncRaftAppendEntries(unstable->entries, entries, n);

    // save into logstore
    assert(pLogStore->logLastIndex(pLogStore->pArg) == entries[0].index);
    for (i = 0; i < n; ++i) {
      SSyncBuffer* buf = syncRaftEncodeRaftEntry(&(entries[i]));
      if (buf == NULL) {
        return RAFT_OOM;
      }
      pLogStore->logWrite(pLogStore->pArg, entries[i].index, buf);
      free(buf);
    }
    assert(pLogStore->logLastIndex(pLogStore->pArg) == entries[n - 1].index);
    syncRaftLogStableTo(pRaft->log, entries[n - 1].index, entries[n - 1].term);
    return ret;
  }

  if (afterIndex <= unstable->offset) {
    syncInfo("replace the unstable entries from index %" PRId64 "", afterIndex);
		// The log is being truncated to before our current offset
		// portion, so set the offset and replace the entries    
    unstable->offset = afterIndex;
    ret = syncRaftAssignEntries(unstable->entries, entries, n);

    // save into logstore
    // first truncate log
    pLogStore->logTruncate(pLogStore->pArg, afterIndex);
    assert(pLogStore->logLastIndex(pLogStore->pArg) == entries[0].index);
    for (i = 0; i < n; ++i) {
      SSyncBuffer* buf = syncRaftEncodeRaftEntry(&(entries[i]));
      if (buf == NULL) {
        return RAFT_OOM;
      }
      pLogStore->logWrite(pLogStore->pArg, entries[i].index, buf);
      free(buf);
    }
    assert(pLogStore->logLastIndex(pLogStore->pArg) == entries[n - 1].index);
    syncRaftLogStableTo(pRaft->log, entries[n - 1].index, entries[n - 1].term);

    return ret;
  }

  assert(afterIndex > unstable->offset);

	// truncate to after and copy to u.entries
	// then append
  syncInfo("truncate the unstable entries before index %" PRId64 "", afterIndex);
  SSyncRaftEntry* sliceEnts;
  int nSliceEnts;

  syncRaftUnstableLogSlice(unstable, unstable->offset, afterIndex, &sliceEnts, &nSliceEnts);

  syncRaftCleanEntryArray(unstable->entries);
  syncRaftAppendEmptyEntry(unstable->entries);
  ret = syncRaftAppendEntries(unstable->entries, sliceEnts, nSliceEnts);
  if (ret < 0) return ret;
  ret = syncRaftAppendEntries(unstable->entries, entries, n);

  // save into logstore
  // first rollback log
  pLogStore->logTruncate(pLogStore->pArg, afterIndex);
  assert(pLogStore->logLastIndex(pLogStore->pArg) == entries[0].index);
  for (i = 0; i < n; ++i) {
    SSyncBuffer* buf = syncRaftEncodeRaftEntry(&(entries[i]));
    if (buf == NULL) {
      return RAFT_OOM;
    }
    pLogStore->logWrite(pLogStore->pArg, entries[i].index, buf);
    free(buf);
  }
  assert(pLogStore->logLastIndex(pLogStore->pArg) == entries[n - 1].index);
  syncRaftLogStableTo(pRaft->log, entries[n - 1].index, entries[n - 1].term);

  return ret;
}

// visit entries in [lo, hi - 1]
void syncRaftUnstableLogVisit(const SSyncRaftUnstableLog* unstable, SyncIndex lo, SyncIndex hi, visitEntryFp visit, void* arg) {
  mustCheckOutOfBounds(unstable, lo, hi);
  while (lo < hi) {
    const SSyncRaftEntry* entry = syncRaftEntryOfPosition(unstable->entries, lo - unstable->offset);
    visit(entry, arg);
  }
  return;
}

SyncIndex syncRaftUnstableLogOffset(const SSyncRaftUnstableLog* unstable) {
  return unstable->offset;
}

void syncRaftUnstableLogSlice(const SSyncRaftUnstableLog* unstable, SyncIndex lo, SyncIndex hi, SSyncRaftEntry** ppEntries, int* n) {
  mustCheckOutOfBounds(unstable, lo, hi);
  syncRaftSliceEntries(unstable->entries, lo - unstable->offset, hi - unstable->offset, ppEntries, n);
}

// u.offset <= lo <= hi <= u.offset+len(u.entries)
static void mustCheckOutOfBounds(const SSyncRaftUnstableLog* unstable, SyncIndex lo, SyncIndex hi) {
  const SSyncRaft* pRaft = unstable->pRaft;
  if (lo > hi) {
    syncFatal("[%d:%d]invalid unstable.slice %" PRId64 " > %" PRId64 "",
      pRaft->selfGroupId, pRaft->selfId, lo, hi);
  }

  SyncIndex upper = unstable->offset + syncRaftNumOfEntries(unstable->entries);
  if (lo < unstable->offset || hi > upper) {
    syncFatal("[%d:%d]unstable.slice[%"PRId64",%"PRId64") out of bound [%"PRId64",%"PRId64"]",
      pRaft->selfGroupId, pRaft->selfId, lo, hi, unstable->offset, upper);
  }
}