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

#ifndef HYPERTABLE_BLOCKCOMPRESSIONHEADERCOMMITLOG_H
#define HYPERTABLE_BLOCKCOMPRESSIONHEADERCOMMITLOG_H

#include "BlockCompressionHeader.h"

namespace Hypertable {

  /**
   * Base class for compressed block header for Cell Store blocks.
   */
  class BlockCompressionHeaderCommitLog : public BlockCompressionHeader {

  public:
    BlockCompressionHeaderCommitLog();
    BlockCompressionHeaderCommitLog(const char *magic, int64_t revision,
            uint64_t cluster_id);

    void set_revision(int64_t revision) { m_revision = revision; }
    int64_t get_revision() { return m_revision; }
    void set_cluster_id(uint64_t cluster_id) { m_cluster_id = cluster_id; }
    uint64_t get_cluster_id() { return m_cluster_id; }

    bool is_legacy() { return (get_magic()[9] != '2'); }

    virtual size_t length() {
      return is_legacy()
          ? BlockCompressionHeader::LENGTH + 8
          : BlockCompressionHeader::LENGTH + 16;
    }

    virtual void encode(uint8_t **bufp);
    virtual void decode(const uint8_t **bufp, size_t *remainp);
    void decode_cluster_id(const uint8_t **bufp, size_t *remainp);

  private:
    int64_t m_revision;
    uint64_t m_cluster_id;
  };

}

#endif // HYPERTABLE_BLOCKCOMPRESSIONHEADERCOMMITLOG_H
