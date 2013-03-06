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

#ifndef REPLICATION_MASTERCLIENT_H
#define REPLICATION_MASTERCLIENT_H

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

#include "Hypertable/Lib/ClusterConfiguration.h"
#include "Hypertable/Lib/ReplicationTypes.h"

namespace Hypertable {

/**
 * Client proxy interface to Replication.Master
 */
class ReplicationMasterClient : public ReferenceCount {
  public:
    /**
     * Constructor
     */
    ReplicationMasterClient(const String &cluster,
            ClusterConfigurationPtr &config);

    ReplicationMasterClient(const InetAddr &addr);

    /**
     * Issues a "assign fragments" request. This call blocks until it receives
     * a response from the server.
     */
    void assign_fragments(const String &location, uint16_t port,
            StringSet &fragments,
            ReplicationTypes::ClusterToSlavesMap &clusters,
            ReplicationTypes::TableIdToClusterMap &table_ids,
            ReplicationTypes::TableIdToTableNameMap &table_names,
            uint32_t timeout_ms = 0);

    /**
     * Issues a "status" request. This call blocks until it receives a
     * response from the server.
     */
    void status(uint32_t timeout_ms = 0);

    /**
     * Issues a "status" request with timer. This call blocks until it
     * receives a response from the server.
     */
    void status(Timer &timer);

    /**
     * Issues a "get receiver_list" request. This call blocks until it
     * receives a response from the server.
     */
    void get_receiver_list(std::vector<InetAddr> &receivers);

    enum {
      SCHEMA_UPDATE_CREATE_TABLE = 1,
      SCHEMA_UPDATE_ALTER_TABLE  = 2
    };

    /**
     * Notifies the local Replication.Master of a Schema update
     * @param what one of the SCHEMA_UPDATE_ enums
     * @table_name full qualified name (with namespace)
     * @table_id Table ID in the local cluster
     */
    void notify_schema_update(int what, const String &table_name,
            const String &table_id, const String &schema);

    /**
     * Applies a schema update on the remote cluster
     * @param what one of the SCHEMA_UPDATE_ enums
     * @table_name full qualified name (with namespace)
     * @schema the new schema; can be empty
     */
    void apply_schema_update(int what, const String &table_name,
            const String &schema);

    /**
     * Issues a "shutdown" request.  This call blocks until it receives a
     * response from the server or times out.
     */
    void shutdown();

    /**
     * Issues a "finished fragment" request.
     */
    void finished_fragment(const String &fragment, int error, uint64_t offset,
            const std::vector<String> &linked_logs);

    /**
     * get all the Master's addresses
     */
    void get_cluster_addresses(std::vector<InetAddr> &addresses) const {
      addresses = m_cluster_addresses;
    }

  private:
    void send_message(CommBufPtr &cbp, DispatchHandler *handler,
            uint32_t timeout_ms);

    Comm *m_comm;
    uint32_t m_default_timeout_ms;
    String m_cluster;
    std::vector<InetAddr> m_cluster_addresses;
    size_t m_active_address;
};

typedef boost::intrusive_ptr<ReplicationMasterClient>
            ReplicationMasterClientPtr;

} // namespace Hypertable

#endif // REPLICATION_MASTERCLIENT_H
