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
#include "Common/Serialization.h"
#include "Common/Time.h"

#include "AsyncComm/CommHeader.h"

#include "ReplicationSlaveProtocol.h"

namespace Hypertable {

using namespace Serialization;

const char *ReplicationSlaveProtocol::m_command_strings[] = {
  "shutdown",
  "get status",
  "update"
};

CommBuf *
ReplicationSlaveProtocol::create_status_request() {
  CommHeader header(COMMAND_STATUS);
  return new CommBuf(header, 0);
}

CommBuf *
ReplicationSlaveProtocol::create_shutdown_request() {
  CommHeader header(COMMAND_SHUTDOWN);
  return new CommBuf(header, 0);
}

CommBuf *
ReplicationSlaveProtocol::create_update_request(const String &table,
            const DynamicBuffer &dbuf) {
  CommHeader header(COMMAND_UPDATE);
  CommBuf *cbuf = new CommBuf(header,
          Serialization::encoded_length_vstr(table) + dbuf.fill());
  cbuf->append_vstr(table);
  cbuf->append_bytes(dbuf.base, dbuf.fill());
  return cbuf;
}

const char *ReplicationSlaveProtocol::command_text(uint64_t command) {
  if (command < 0 || command >= COMMAND_MAX)
    return "UNKNOWN";
  return m_command_strings[command];
}

} // namespace Hypertable

