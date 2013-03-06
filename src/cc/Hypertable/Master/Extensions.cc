/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License.
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

#include "Hyperspace/Session.h"

#include "Extensions.h"
#include "OperationAlterTable.h"
#include "OperationCreateTable.h"


namespace Hypertable {

using namespace Hyperspace;

PropertiesPtr Extensions::ms_props;
ReplicationMasterClientPtr Extensions::ms_replication_client;
ClusterConfigurationPtr Extensions::ms_cluster_config;


void
Extensions::initialize(PropertiesPtr &props) {
  ms_props = props;
  ms_cluster_config = new ClusterConfiguration(ms_props);
}

void
Extensions::validate_create_table_schema(SchemaPtr &schema) {
  // check if replication is enabled and if the remote replication cluster
  // is properly configured
  Schema::ReplicationClusterMap::const_iterator it;
  for (it = schema->get_replication_cluster().begin();
      it != schema->get_replication_cluster().end(); ++it) {
    if (!Extensions::ms_cluster_config->has_remote_cluster(it->first)) {
      HT_THROW(Error::MASTER_BAD_SCHEMA,
              Hypertable::format("Unknown replication cluster '%s'",
                  it->first.c_str()));
    }
  }
}

void
Extensions::validate_alter_table_schema(SchemaPtr &schema) {
  // check if replication is enabled and if the remote replication cluster
  // is properly configured
  Schema::ReplicationClusterMap::const_iterator it;
  for (it = schema->get_replication_cluster().begin();
      it != schema->get_replication_cluster().end(); ++it) {
    if (!Extensions::ms_cluster_config->has_remote_cluster(it->first)) {
      HT_THROW(Error::MASTER_BAD_SCHEMA,
              Hypertable::format("Unknown replication cluster '%s'",
                  it->first.c_str()));
    }
  }
}

bool
Extensions::alter_table_extension(OperationAlterTable *op,
            const String &schema_string, const String &table_name,
            const String &table_id) {
  // inform the local Replication.Master about the update
  if (op->get_context()->props->get_bool("Hypertable.RangeServer.CommitLog.FragmentRemoval.Disable")) {
    if (table_name.find("/sys") != 0
            && Filesystem::basename(table_name)[0] != '^') {
      try {
        ReplicationMasterClientPtr ptr;
        ptr = Extensions::get_replication_client(op->get_context());
        if (!ptr)
          HT_THROW(Error::REPLICATION_CLUSTER_NOT_FOUND,
                  "Unable to connect to Replication.Master");
        ptr->notify_schema_update(
                ReplicationMasterClient::SCHEMA_UPDATE_ALTER_TABLE,
                table_name, table_id, schema_string);
      }
      catch (Exception &ex) {
        Extensions::ms_replication_client = 0;
        op->complete_error(ex);
        return false;
      }
    }
  }
  return true;
}

bool
Extensions::create_table_extension(OperationCreateTable *op,
            const String &schema_string, const String &table_name,
            TableIdentifierManaged &table_id) {
  // inform the local Replication.Master about the update
  if (table_name.find("/sys") != 0
      && Filesystem::basename(table_name)[0] != '^') {
    try {
      SchemaPtr s = Schema::new_instance(schema_string.c_str(),
              schema_string.size());
      if (s->get_replication_cluster().size()) {
        ReplicationMasterClientPtr ptr;
        ptr = get_replication_client(op->get_context());
        if (!ptr)
          HT_THROW(Error::REPLICATION_CLUSTER_NOT_FOUND,
                  "Unable to connect to Replication.Master");
        ptr->notify_schema_update(
              ReplicationMasterClient::SCHEMA_UPDATE_CREATE_TABLE,
              table_name, table_id.id, schema_string);
      }
    }
    catch (Exception &ex) {
      Extensions::ms_replication_client = 0;
      op->complete_error(ex);
      return false;
    }
  }
  return true;
}

ReplicationMasterClientPtr
Extensions::get_replication_client(ContextPtr &context) {
  if (ms_replication_client.get())
    return ms_replication_client;

  uint64_t handle = 0;
  DynamicBuffer value;

  String address_str;

  address_str = context->props->get_str("Hypertable.Replication.Slave.MasterAddress");
  if (address_str.empty()) {
    try {
      HT_ON_SCOPE_EXIT(&Hyperspace::close_handle_ptr,
              context->hyperspace, &handle);

      String path = context->toplevel_dir + "/replication/master";
      handle = context->hyperspace->open(path, OPEN_FLAG_READ);
      context->hyperspace->attr_get(handle, "location", value);
      address_str = (const char *)value.base;
    }
    catch (Exception &e) {
      HT_ERROR_OUT << e << HT_END;
      return 0;
    }
  }

  InetAddr master_address = InetAddr(address_str.c_str());
  HT_INFO_OUT << "Replication.Master address is " << master_address
      << HT_END;
 
  // TODO make timeout configurable
  context->conn_manager->add(master_address, 10000, "Replication.Master");

  return new ReplicationMasterClient(master_address);
}

} // namespace Hypertable
