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

#ifndef REPLICATION_SLAVECLIENT_H
#define REPLICATION_SLAVECLIENT_H

#include <boost/intrusive_ptr.hpp>
#include <vector>
#include <map>

#include "Common/InetAddr.h"
#include "Common/StaticBuffer.h"
#include "Common/ReferenceCount.h"
#include "Common/StringExt.h"

#include "AsyncComm/Comm.h"
#include "AsyncComm/CommBuf.h"
#include "AsyncComm/DispatchHandler.h"

namespace Hypertable {

/**
 * Client proxy interface to Replication.Slave
 */
class ReplicationSlaveClient : public ReferenceCount {
  public:
    /**
     * Constructor
     */
    ReplicationSlaveClient(const std::vector<InetAddr> &addresses);

    /**
     * Issues a "shutdown" request.  This call blocks until it receives a
     * response from the server or times out.
     */
    void shutdown();

    /**
     * sends a block of updates to a remote cluster
     */
    void update(const String &table, const DynamicBuffer &dbuf);

    /**
     * get the currently active address
     */
    const InetAddr &get_current_address() const {
      return m_cluster_addresses[m_active_address];
    }

  private:
    void send_message(CommBufPtr &cbp, DispatchHandler *handler,
            uint32_t timeout_ms);

    Comm *m_comm;
    uint32_t m_default_timeout_ms;
    std::vector<InetAddr> m_cluster_addresses;
    size_t m_active_address;
};

typedef boost::intrusive_ptr<ReplicationSlaveClient> ReplicationSlaveClientPtr;

} // namespace Hypertable

#endif // REPLICATION_SLAVECLIENT_H
