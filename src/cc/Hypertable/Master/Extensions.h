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

#ifndef HYPERTABLE_MASTER_EXTENSIONS_H
#define HYPERTABLE_MASTER_EXTENSIONS_H

#include "Common/Compat.h"
#include "Common/HashMap.h"

#include "Hypertable/Lib/Types.h"
#include "Hypertable/Lib/Schema.h"
#include "Hypertable/Lib/ReplicationMasterClient.h"
#include "Hypertable/Lib/ClusterConfiguration.h"

#include "Context.h"

namespace Hypertable {

class OperationAlterTable;
class OperationCreateTable;

class Extensions {
  public:
    static void validate_create_table_schema(SchemaPtr &schema);
    static void validate_alter_table_schema(SchemaPtr &schema);

    static void initialize(PropertiesPtr &props);

    // return false to abort the operation; in that case make sure
    // to call op->complete_error()!
    static bool alter_table_extension(OperationAlterTable *op,
            const String &schema_string, const String &table_name,
            const String &table_id);

    // return false to abort the operation; in that case make sure
    // to call op->complete_error()!
    static bool create_table_extension(OperationCreateTable *op,
            const String &schema_string, const String &table_name,
            TableIdentifierManaged &table_id);

  private:
    static ReplicationMasterClientPtr get_replication_client(ContextPtr &context);

    static PropertiesPtr ms_props;
    static ReplicationMasterClientPtr ms_replication_client;
    static ClusterConfigurationPtr ms_cluster_config;
};

} // namespace Hypertable

#endif // HYPERTABLE_MASTER_EXTENSIONS_H
