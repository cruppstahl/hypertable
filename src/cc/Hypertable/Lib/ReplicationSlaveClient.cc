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
#include "Common/Config.h"
#include "Common/Error.h"
#include "Common/StringExt.h"
#include "Common/Serialization.h"

#include "AsyncComm/DispatchHandlerSynchronizer.h"

#include "Hypertable/Lib/ReplicationSlaveProtocol.h"

#include "ReplicationSlaveClient.h"

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace Hypertable::Serialization;

ReplicationSlaveClient::ReplicationSlaveClient(const std::vector<InetAddr> &addresses)
  : m_comm(Comm::instance()), m_cluster_addresses(addresses),
      m_active_address(0) {
  m_default_timeout_ms = get_i32("Hypertable.Request.Timeout");
  // pick a random slave in order to spread the load evenly
  // TODO this might be improved by getting an assignment from the master
  m_active_address = rand() % m_cluster_addresses.size();
}

void
ReplicationSlaveClient::shutdown() {
  CommBufPtr cbp(ReplicationSlaveProtocol::create_shutdown_request());
  send_message(cbp, 0, m_default_timeout_ms);
}

void
ReplicationSlaveClient::update(const String &table, const DynamicBuffer &dbuf) {
  DispatchHandlerSynchronizer sync_handler;
  EventPtr event;
  CommBufPtr cbp(ReplicationSlaveProtocol::create_update_request(table, dbuf));
  send_message(cbp, &sync_handler, m_default_timeout_ms);

  if (!sync_handler.wait_for_reply(event))
    HT_THROW((int)Protocol::response_code(event),
            String("Replication.Slave update() failure: ")
            + Protocol::string_format_message(event));
}

void
ReplicationSlaveClient::send_message(CommBufPtr &cbp, 
        DispatchHandler *handler, uint32_t timeout_ms) {
  if (m_active_address >= m_cluster_addresses.size())
    HT_THROW(Error::REPLICATION_CLUSTER_NOT_FOUND,
            "No addresses available for this slave");

  InetAddr &addr = m_cluster_addresses[m_active_address];

  HT_INFO_OUT << "Sending message to remote slave " << addr << HT_END;

  int error;
  try {
    error = m_comm->send_request(addr, timeout_ms, cbp, handler);
  }
  catch (Exception &ex) {
    error = ex.code();
  }

  if (error != Error::OK) {
    // on error: round-robin to the next slave
    // TODO only do that if it's a network error, not a protocol error!?
    if (++m_active_address >= m_cluster_addresses.size())
      m_active_address = 0;
    HT_WARN_OUT << "Comm::send_request to " << addr << " failed: "
       <<  Error::get_text(error) << "; next cluster address is #"
       << m_active_address << " ("
       << m_cluster_addresses[m_active_address] << ")" << HT_END;
    HT_THROWF(error, "Comm::send_request to %s failed",
              addr.format().c_str());
  }
}
