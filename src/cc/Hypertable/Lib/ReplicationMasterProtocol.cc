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

#include "ReplicationMasterProtocol.h"

namespace Hypertable {

using namespace Serialization;

const char *ReplicationMasterProtocol::m_command_strings[] = {
  "shutdown",
  "assign fragments",
  "finished fragment",
  "get status",
  "apply schema update",
  "notify schema update",
  "get receiver list"
};

CommBuf *
ReplicationMasterProtocol::create_shutdown_request() {
  CommHeader header(COMMAND_SHUTDOWN);
  return new CommBuf(header, 0);
}

CommBuf *
ReplicationMasterProtocol::create_assign_fragments_request(const String &location,
        uint16_t port) {
  CommHeader header(COMMAND_ASSIGN_FRAGMENTS);
  CommBuf *cbuf = new CommBuf(header, 2 + encoded_length_vstr(location));
;
  cbuf->append_vstr(location);
  cbuf->append_i16(port);
  return cbuf;
}

CommBuf *
ReplicationMasterProtocol::create_finished_fragment_request(
        const String &fragment, int error, uint64_t offset,
        const std::vector<String> &linked_logs) {
  CommHeader header(COMMAND_FINISHED_FRAGMENT);

  size_t len = 4 + 8 + encoded_length_vstr(fragment) + 4;
  foreach_ht (const String &s, linked_logs)
    len += encoded_length_vstr(s);

  CommBuf *cbuf = new CommBuf(header, len);
  cbuf->append_i32(error);
  cbuf->append_vstr(fragment);
  cbuf->append_i64(offset);
  cbuf->append_i32(linked_logs.size());
  foreach_ht (const String &s, linked_logs)
    cbuf->append_vstr(s);
  return cbuf;
}

CommBuf *
ReplicationMasterProtocol::create_status_request() {
  CommHeader header(COMMAND_STATUS);
  return new CommBuf(header, 0);
}

CommBuf *
ReplicationMasterProtocol::create_get_receiver_list_request() {
  CommHeader header(COMMAND_GET_RECEIVER_LIST);
  return new CommBuf(header, 0);
}

CommBuf *
ReplicationMasterProtocol::create_apply_schema_update_request(int what,
        const String &table_name, const String &schema) {
  CommHeader header(COMMAND_APPLY_SCHEMA_UPDATE);
  CommBuf *cbuf = new CommBuf(header,
          4
          + encoded_length_vstr(table_name)
          + encoded_length_vstr(schema));
  cbuf->append_i32(what);
  cbuf->append_vstr(table_name);
  cbuf->append_vstr(schema);
  return cbuf;
}

CommBuf *
ReplicationMasterProtocol::create_notify_schema_update_request(int what,
        const String &table_name, const String &table_id,
        const String &schema) {
  CommHeader header(COMMAND_NOTIFY_SCHEMA_UPDATE);
  CommBuf *cbuf = new CommBuf(header,
          4
          + encoded_length_vstr(table_name)
          + encoded_length_vstr(table_id)
          + encoded_length_vstr(schema));
  cbuf->append_i32(what);
  cbuf->append_vstr(table_name);
  cbuf->append_vstr(table_id);
  cbuf->append_vstr(schema);
  return cbuf;
}

const char *ReplicationMasterProtocol::command_text(uint64_t command) {
  if (command < 0 || command >= COMMAND_MAX)
    return "UNKNOWN";
  return m_command_strings[command];
}

} // namespace Hypertable

