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

#ifndef _TD_LIBS_SYNC_RAFT_MESSAGE_H
#define _TD_LIBS_SYNC_RAFT_MESSAGE_H

#include <stdio.h>
#include "sync.h"
#include "sync_type.h"

/** 
 * below define message type which handled by Raft.
 * 
 * internal message, which communicate between threads, start with RAFT_MSG_INTERNAL_*.
 * internal message use pointer only and stack memory, need not to be decode/encode and free.
 * 
 * outter message start with RAFT_MSG_*, which communicate between cluster peers,
 * need to implement its decode/encode functions.
 **/
typedef enum ESyncRaftMessageType {
  // client propose a cmd
  RAFT_MSG_INTERNAL_PROP = 1,

  // node election timeout
  RAFT_MSG_INTERNAL_ELECTION = 2,

  RAFT_MSG_INTERNAL_BEAT = 3,

  RAFT_MSG_VOTE = 3,
  RAFT_MSG_VOTE_RESP = 4,

  RAFT_MSG_APPEND = 5,
  RAFT_MSG_APPEND_RESP = 6,

  RAFT_MSG_HEARTBEAT = 7,

  RAFT_MSG_SNAPSHOT,

  RAFT_MSG_READ_INDEX,
} ESyncRaftMessageType;

static const char* gMsgString[] = {

};

typedef struct RaftMsgInternal_Prop {
  const SSyncBuffer *pBuf;
  bool isWeak;
  void* pData;
  ESyncRaftEntryType eType;
} RaftMsgInternal_Prop;

typedef struct RaftMsgInternal_Election {

} RaftMsgInternal_Election;

typedef struct RaftMsgInternal_Beat {

} RaftMsgInternal_Beat;

typedef struct RaftMsg_Vote {
  ESyncRaftElectionType cType;
  SyncIndex lastIndex;
  SyncTerm lastTerm;
} RaftMsg_Vote;

typedef struct RaftMsg_VoteResp {
  bool rejected;
  ESyncRaftElectionType cType;
} RaftMsg_VoteResp;

typedef struct RaftMsg_Append_Entries {
  // index of log entry preceeding new ones
  SyncIndex index;

  // term of entry at prevIndex
  SyncTerm term;

  // leader's commit index.
  SyncIndex commitIndex;

  // size of the log entries array
  int nEntries;

  // log entries array
  SSyncRaftEntry* entries;
} RaftMsg_Append_Entries;

typedef struct RaftMsg_Append_Resp {
  SyncIndex index;
  SyncIndex rejectHint;
  SyncTerm logTerm;
  bool reject;
} RaftMsg_Append_Resp;

typedef struct RaftMsg_Heartbeat {
  SyncIndex commmitIndex;
} RaftMsg_Heartbeat;

typedef struct RaftMsg_Heartbeat_Resp {
  
} RaftMsg_Heartbeat_Resp;

typedef struct SSyncMessage {
  ESyncRaftMessageType msgType;
  SyncTerm term;
  SyncGroupId groupId;
  SyncNodeId from;

  union {
    RaftMsgInternal_Prop propose;

    RaftMsgInternal_Election election;

    RaftMsgInternal_Beat beat;
  
    RaftMsg_Vote vote;
    RaftMsg_VoteResp voteResp;

    RaftMsg_Append_Entries appendEntries;
    RaftMsg_Append_Resp appendResp;

    RaftMsg_Heartbeat heartbeat;
  };
} SSyncMessage;

static FORCE_INLINE SSyncMessage* syncInitPropEntryMsg(SSyncMessage* pMsg, const SSyncBuffer* pBuf, void* pData, bool isWeak) {
  *pMsg = (SSyncMessage) {
    .msgType = RAFT_MSG_INTERNAL_PROP,
    .term = 0,
    .propose = (RaftMsgInternal_Prop) {
      .isWeak = isWeak,
      .pBuf = pBuf,
      .pData = pData,
      .eType = SYNC_ENTRY_TYPE_LOG,
    },
  };

  return pMsg;
}

static FORCE_INLINE SSyncMessage* syncInitElectionMsg(SSyncMessage* pMsg, SyncNodeId from) {
  *pMsg = (SSyncMessage) {
    .msgType = RAFT_MSG_INTERNAL_ELECTION,
    .term = 0,
    .from = from,
    .election = (RaftMsgInternal_Election) {

    },
  };

  return pMsg;
}

static FORCE_INLINE SSyncMessage* syncInitBeatMsg(SSyncMessage* pMsg, SyncNodeId from) {
  *pMsg = (SSyncMessage) {
    .msgType = RAFT_MSG_INTERNAL_BEAT,
    .term = 0,
    .from = from,
    .beat = (RaftMsgInternal_Beat) {

    },
  };

  return pMsg;
}

static FORCE_INLINE SSyncMessage* syncNewVoteMsg(SyncGroupId groupId, SyncNodeId from,
                                                SyncTerm term, ESyncRaftElectionType cType, 
                                                SyncIndex lastIndex, SyncTerm lastTerm) {
  SSyncMessage* pMsg = (SSyncMessage*)malloc(sizeof(SSyncMessage));
  if (pMsg == NULL) {
    return NULL;
  }
  *pMsg = (SSyncMessage) {
    .groupId = groupId,
    .from = from,
    .term = term,
    .msgType = RAFT_MSG_VOTE,
    .vote = (RaftMsg_Vote) {
      .cType = cType,
      .lastIndex = lastIndex,
      .lastTerm = lastTerm,
    },
  };

  return pMsg;
}

static FORCE_INLINE SSyncMessage* syncNewVoteRespMsg(SyncGroupId groupId, SyncNodeId from,
                                                  ESyncRaftElectionType cType, bool rejected) {
  SSyncMessage* pMsg = (SSyncMessage*)malloc(sizeof(SSyncMessage));
  if (pMsg == NULL) {
    return NULL;
  }
  *pMsg = (SSyncMessage) {
    .groupId = groupId,
    .from = from,
    .msgType = RAFT_MSG_VOTE_RESP,
    .voteResp = (RaftMsg_VoteResp) {
      .cType = cType,
      .rejected = rejected,
    },
  };

  return pMsg;
}

static FORCE_INLINE SSyncMessage* syncNewAppendMsg(SyncGroupId groupId, SyncNodeId from,
                                                  SyncTerm term, SyncIndex logIndex, SyncTerm logTerm,
                                                  SyncIndex commitIndex, int nEntries, SSyncRaftEntry* entries) {
  SSyncMessage* pMsg = (SSyncMessage*)malloc(sizeof(SSyncMessage));
  if (pMsg == NULL) {
    return NULL;
  }
  *pMsg = (SSyncMessage) {
    .groupId = groupId,
    .from = from,
    .term = term,
    .msgType = RAFT_MSG_APPEND,
    .appendEntries = (RaftMsg_Append_Entries) {
      .index = logIndex,
      .term = logTerm,
      .commitIndex = commitIndex,
      .nEntries = nEntries,
      .entries = entries,
    },
  };

  return pMsg;
}

static FORCE_INLINE SSyncMessage* syncNewEmptyAppendRespMsg(SyncGroupId groupId, SyncNodeId from, SyncTerm term) {
  SSyncMessage* pMsg = (SSyncMessage*)malloc(sizeof(SSyncMessage));
  if (pMsg == NULL) {
    return NULL;
  }
  *pMsg = (SSyncMessage) {
    .groupId = groupId,
    .from = from,
    .term = term,
    .msgType = RAFT_MSG_APPEND_RESP,
    .appendResp = (RaftMsg_Append_Resp) {
      .index = 0,
      .reject = false,
    },
  };

  return pMsg;
}

static FORCE_INLINE bool syncIsInternalMsg(ESyncRaftMessageType msgType) {
  return msgType == RAFT_MSG_INTERNAL_PROP ||
         msgType == RAFT_MSG_INTERNAL_ELECTION;
}

static FORCE_INLINE bool syncIsPreVoteRespMsg(const SSyncMessage* pMsg) {
  return pMsg->msgType == RAFT_MSG_VOTE_RESP && pMsg->voteResp.cType == SYNC_RAFT_CAMPAIGN_PRE_ELECTION;
}

static FORCE_INLINE bool syncIsVoteMsg(const SSyncMessage* pMsg) {
  return pMsg->msgType == RAFT_MSG_VOTE;
}

static FORCE_INLINE bool syncIsPreVoteMsg(const SSyncMessage* pMsg) {
  return pMsg->msgType == RAFT_MSG_VOTE && pMsg->voteResp.cType == SYNC_RAFT_CAMPAIGN_PRE_ELECTION;
}

void syncFreeMessage(const SSyncMessage* pMsg);

// message handlers
int syncRaftHandleElectionMessage(SSyncRaft* pRaft, SSyncMessage* pMsg);
int syncRaftHandleVoteMessage(SSyncRaft* pRaft, SSyncMessage* pMsg);
int syncRaftHandleVoteRespMessage(SSyncRaft* pRaft, SSyncMessage* pMsg);
int syncRaftHandleAppendEntriesMessage(SSyncRaft* pRaft, SSyncMessage* pMsg);
int syncRaftHandleAppendEntriesRespMessage(SSyncRaft* pRaft, SSyncMessage* pMsg, SSyncRaftProgress*);
int syncRaftHandlePropMessage(SSyncRaft* pRaft, SSyncMessage* pMsg);
int syncRaftHandleHeartbeat(SSyncRaft* pRaft, SSyncMessage* pMsg);

#endif  /* _TD_LIBS_SYNC_RAFT_MESSAGE_H */