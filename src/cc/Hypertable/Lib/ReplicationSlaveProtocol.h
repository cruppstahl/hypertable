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

#ifndef REPLICATION_SLAVE_PROTOCOL_H
#define REPLICATION_SLAVE_PROTOCOL_H

#include "Common/StatsSystem.h"

#include "AsyncComm/CommBuf.h"
#include "AsyncComm/Event.h"
#include "AsyncComm/Protocol.h"

namespace Hypertable {

class ReplicationSlaveProtocol : public Protocol {
  public:
    static const uint64_t COMMAND_SHUTDOWN              = 0;
    static const uint64_t COMMAND_STATUS                = 1;
    static const uint64_t COMMAND_UPDATE                = 2;
    static const uint64_t COMMAND_MAX                   = 3;

    static CommBuf *create_shutdown_request();

    static CommBuf *create_status_request();

    static CommBuf *create_update_request(const String &table,
            const DynamicBuffer &dbuf);

    virtual const char *command_text(uint64_t command);

  private:
    static const char *m_command_strings[];
};

} // namespace Hypertable

#endif // REPLICATION_SLAVE_PROTOCOL_H
