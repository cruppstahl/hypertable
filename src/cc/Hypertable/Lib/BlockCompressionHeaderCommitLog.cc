/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License, or any later version.
 *
 * Hypertable is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#include "Common/Compat.h"
#include "Common/Error.h"
#include "Common/Logger.h"

#include "Common/Serialization.h"

#include "BlockCompressionHeaderCommitLog.h"

using namespace Hypertable;
using namespace Serialization;

BlockCompressionHeaderCommitLog::BlockCompressionHeaderCommitLog()
  : BlockCompressionHeader(), m_revision(0), m_cluster_id(0) {
}

BlockCompressionHeaderCommitLog::BlockCompressionHeaderCommitLog(
    const char *magic, int64_t revision, uint64_t cluster_id)
  : BlockCompressionHeader(magic), m_revision(revision),
    m_cluster_id(cluster_id) {
}

void BlockCompressionHeaderCommitLog::encode(uint8_t **bufp) {
  uint8_t *base = *bufp;
  BlockCompressionHeader::encode(bufp);
  encode_i64(bufp, m_revision);
  if (!is_legacy())
    encode_i64(bufp, m_cluster_id);
  write_header_checksum(base, bufp);
}

void BlockCompressionHeaderCommitLog::decode(const uint8_t **bufp,
        size_t *remainp) {
  BlockCompressionHeader::decode(bufp, remainp);
  m_revision = decode_i64(bufp, remainp);
  if (!is_legacy())
    m_cluster_id = decode_i64(bufp, remainp);
  else
    m_cluster_id = 0;
  // skip checksum
  *bufp += 2;
  *remainp -= 2;
}

