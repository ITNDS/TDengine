/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
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

#ifndef _TD_META_TABLE_CFG_H_
#define _TD_META_TABLE_CFG_H_

#include "meta.h"

#ifdef __cplusplus
extern "C" {
#endif

#define META_SUPER_TABLE 0
#define META_CHILD_TABLE 1
#define META_NORMAL_TABLE 2

int    metaValidateTbOptions(SMeta *pMeta, const STbCfg *);
size_t metaEncodeTbObjFromTbOptions(const STbCfg *, void *pBuf, size_t bsize);

#ifdef __cplusplus
}
#endif

#endif /*_TD_META_TABLE_CFG_H_*/