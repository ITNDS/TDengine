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

#ifndef _TD_LIBS_SYNC_RAFT_CODE_H
#define _TD_LIBS_SYNC_RAFT_CODE_H

typedef enum ESyncRaftCode {
  RAFT_OK = 0,

  /**
   * RAFT_INDEX_COMPACTED is returned when a request index is predates the last snapshot
   **/
  RAFT_INDEX_COMPACTED = -1,

  /**
   * RAFT_INDEX_UNAVAILABLE is returned when a request index is unavailable
   **/
  RAFT_INDEX_UNAVAILABLE = -2,

  RAFT_NO_MEM = -3,
} ESyncRaftCode;

#endif // _TD_LIBS_SYNC_RAFT_CODE_H