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

#ifndef REPLICATION_MASTER_PROTOCOL_H
#define REPLICATION_MASTER_PROTOCOL_H

#include "Common/StatsSystem.h"

#include "AsyncComm/CommBuf.h"
#include "AsyncComm/Event.h"
#include "AsyncComm/Protocol.h"

namespace Hypertable {

class ReplicationMasterProtocol : public Protocol {
  public:
    static const uint64_t COMMAND_SHUTDOWN              = 0;
    static const uint64_t COMMAND_ASSIGN_FRAGMENTS      = 1;
    static const uint64_t COMMAND_FINISHED_FRAGMENT     = 2;
    static const uint64_t COMMAND_STATUS                = 3;
    static const uint64_t COMMAND_APPLY_SCHEMA_UPDATE   = 4;
    static const uint64_t COMMAND_NOTIFY_SCHEMA_UPDATE  = 5;
    static const uint64_t COMMAND_GET_RECEIVER_LIST     = 6;
    static const uint64_t COMMAND_MAX                   = 7;

    static CommBuf *create_notify_schema_update_request(int what,
                const String &table_name, const String &table_id,
                const String &schema);

    static CommBuf *create_apply_schema_update_request(int what,
                const String &table_name, const String &schema);

    static CommBuf *create_shutdown_request();

    static CommBuf *create_assign_fragments_request(const String &location,
                uint16_t port);

    static CommBuf *create_finished_fragment_request(const String &fragment,
            int error, uint64_t offset, const std::vector<String> &linked_logs);

    static CommBuf *create_status_request();

    static CommBuf *create_get_receiver_list_request();

    virtual const char *command_text(uint64_t command);

  private:
    static const char *m_command_strings[];
};

} // namespace Hypertable

#endif // REPLICATION_MASTER_PROTOCOL_H
