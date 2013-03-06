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

#include "Hypertable/Lib/ReplicationMasterProtocol.h"
#include "Hypertable/Lib/ReplicationMasterClient.h"

using namespace Hypertable;
using namespace Hypertable::Config;
using namespace Hypertable::Serialization;

ReplicationMasterClient::ReplicationMasterClient(const String &cluster,
        ClusterConfigurationPtr &config)
  : m_comm(Comm::instance()), m_cluster(cluster), m_active_address(0) {
  m_default_timeout_ms = get_i32("Hypertable.Request.Timeout");
  m_cluster_addresses = config->get_cluster_addresses(cluster);;
}

ReplicationMasterClient::ReplicationMasterClient(const InetAddr &addr)
  : m_comm(Comm::instance()), m_active_address(0) {
  m_default_timeout_ms = get_i32("Hypertable.Request.Timeout");
  m_cluster_addresses.push_back(addr);
}

void
ReplicationMasterClient::status(Timer &timer) {
  status(timer.remaining());
}

void
ReplicationMasterClient::status(uint32_t timeout_ms) {
  if (!timeout_ms)
    timeout_ms = m_default_timeout_ms;

  DispatchHandlerSynchronizer sync_handler;
  EventPtr event;
  CommBufPtr cbp(ReplicationMasterProtocol::create_status_request());
  send_message(cbp, &sync_handler, timeout_ms);

  if (!sync_handler.wait_for_reply(event))
    HT_THROW((int)Protocol::response_code(event),
            String("Replication.Master status() failure: ")
            + Protocol::string_format_message(event));
}

void
ReplicationMasterClient::get_receiver_list(std::vector<InetAddr> &receivers) {
  DispatchHandlerSynchronizer sync_handler;
  EventPtr event;
  CommBufPtr cbp(ReplicationMasterProtocol::create_get_receiver_list_request());
  send_message(cbp, &sync_handler, m_default_timeout_ms);

  if (!sync_handler.wait_for_reply(event))
    HT_THROW((int)Protocol::response_code(event),
            String("Replication.Master get_receiver_list() failure: ")
            + Protocol::string_format_message(event));

  const uint8_t *ptr = event->payload + 4;
  size_t remain = event->payload_len - 4;
  int32_t count = decode_i32(&ptr, &remain);
  for (int i = 0; i < count; i++) {
    InetAddr addr;
    addr.decode(&ptr, &remain);
    receivers.push_back(addr);
  }
}

void
ReplicationMasterClient::assign_fragments(const String &location, uint16_t port,
        StringSet &fragments, ReplicationTypes::ClusterToSlavesMap &clusters,
        ReplicationTypes::TableIdToClusterMap &table_ids,
        ReplicationTypes::TableIdToTableNameMap &table_names,
        uint32_t timeout_ms) {
  if (!timeout_ms)
    timeout_ms = m_default_timeout_ms;

  DispatchHandlerSynchronizer sync_handler;
  EventPtr event;
  CommBufPtr cbp(ReplicationMasterProtocol::create_assign_fragments_request(location, port));
  send_message(cbp, &sync_handler, timeout_ms);

  if (!sync_handler.wait_for_reply(event))
    HT_THROW((int)Protocol::response_code(event),
            String("Replication.Master assign_fragments() failure: ")
            + Protocol::string_format_message(event));

  const uint8_t *ptr = event->payload + 4;
  size_t remain = event->payload_len - 4;

  // read all tables
  table_ids.clear();
  table_names.clear();
  int32_t count = Serialization::decode_i32(&ptr, &remain);
  for (int i = 0; i < count; i++) {
    String t = Serialization::decode_vstr(&ptr, &remain);
    String n = Serialization::decode_vstr(&ptr, &remain);
    int32_t veccount = Serialization::decode_i32(&ptr, &remain);
    std::vector<String> vec;
    for (int j = 0; j < veccount; j++)
      vec.push_back(Serialization::decode_vstr(&ptr, &remain));
    table_ids[t] = vec;
    table_names[t] = n;
  }

  // read all fragments
  fragments.clear();
  count = Serialization::decode_i32(&ptr, &remain);
  for (int i = 0; i < count; i++)
    fragments.insert(Serialization::decode_vstr(&ptr, &remain));

  // read the cluster info
  clusters.clear();
  count = Serialization::decode_i32(&ptr, &remain);
  for (int i = 0; i < count; i++) {
    std::vector<InetAddr> slaves;
    String cluster = Serialization::decode_vstr(&ptr, &remain);
    int32_t addr_count = Serialization::decode_i32(&ptr, &remain);
    for (int j = 0; j < addr_count; j++) {
      InetAddr addr;
      addr.decode(&ptr, &remain);
      slaves.push_back(addr);
    }
    clusters[cluster] = slaves;
  }
}

void
ReplicationMasterClient::notify_schema_update(int what,
        const String &table_name, const String &table_id,
        const String &schema) {
  DispatchHandlerSynchronizer sync_handler;
  EventPtr event;
  CommBufPtr cbp(ReplicationMasterProtocol::create_notify_schema_update_request(what,
              table_name, table_id, schema));
  send_message(cbp, &sync_handler, m_default_timeout_ms);

  if (!sync_handler.wait_for_reply(event))
    HT_THROW((int)Protocol::response_code(event),
            String("Replication.Master notify_schema_update() failure: ")
            + Protocol::string_format_message(event));
}

void
ReplicationMasterClient::apply_schema_update(int what, const String &table_name,
        const String &schema) {
  DispatchHandlerSynchronizer sync_handler;
  EventPtr event;
  CommBufPtr cbp(ReplicationMasterProtocol::create_apply_schema_update_request(
              what, table_name, schema));
  send_message(cbp, &sync_handler, m_default_timeout_ms);

  if (!sync_handler.wait_for_reply(event))
    HT_THROW((int)Protocol::response_code(event),
            String("Replication.Master apply_schema_update() failure: ")
            + Protocol::string_format_message(event));
}

void
ReplicationMasterClient::shutdown() {
  CommBufPtr cbp(ReplicationMasterProtocol::create_shutdown_request());
  send_message(cbp, 0, m_default_timeout_ms);
}

void
ReplicationMasterClient::finished_fragment(const String &fragment, int error,
        uint64_t offset, const std::vector<String> &linked_logs) {
  CommBufPtr cbp(ReplicationMasterProtocol::create_finished_fragment_request(
              fragment, error, offset, linked_logs));
  send_message(cbp, 0, m_default_timeout_ms);
}

void
ReplicationMasterClient::send_message(CommBufPtr &cbp, 
        DispatchHandler *handler, uint32_t timeout_ms) {
  if (m_active_address >= m_cluster_addresses.size())
    HT_THROWF(Error::REPLICATION_CLUSTER_NOT_FOUND,
            "No addresses available for cluster %s", m_cluster.c_str());

  InetAddr &addr = m_cluster_addresses[m_active_address];

  int error;
  if ((error = m_comm->send_request(addr, timeout_ms, cbp, handler))
      != Error::OK) {
    // on error: round-robin to the next master
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
