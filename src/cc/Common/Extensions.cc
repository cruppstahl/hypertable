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
#include "Common/Config.h"

#include "Extensions.h"


namespace Hypertable {

typedef hash_map<int, const char *> TextMap;

void
Extensions::build_text_map(TextMap *map) {
  (*map)[Error::REPLICATION_CLUSTER_NOT_FOUND]
      = "REPLICATION cluster not found";
}

void
Extensions::add_configuration() {
  using namespace Property;

  Config::file_desc().add_options()
    ("Hypertable.Replication.Master.Port", i16()->default_value(38100),
        "Default port of the Replication Masters")
    ("Hypertable.Replication.Master.Interval", i32()->default_value(30000),
        "Timer interval in milliseconds for retrieving the server state of the remote cluster")
    ("Hypertable.Replication.Timer.Interval", i32()->default_value(10000),
        "Timer interval in milliseconds till updates are sent to the remote cluster")
    ("Hypertable.Replication.BaseNamespace", str()->default_value("/"),
        "Other namespaces are created relative to this base namespace; only for testing")
    ("Hypertable.Replication.TestMode", boo()->default_value(false),
        "Do not send schema updates to the remote cluster, do not grab hyperspace lock on startup; only for testing")
    ("Hypertable.Replication.Slave.Port", i16()->default_value(38101),
        "Default port of the Replication Slaves")
    ("Hypertable.Replication.Slave.MasterAddress", str()->default_value(""),
        "Forces use of a cluster's Replication.Master address instead of "
        "reading it from Hyperspace; only for testing")
    ("Hypertable.Replication.Slave.ProxyName", str()->default_value(""),
        "Use this value for the proxy name (if set) instead of reading from run dir.")
    ("Hypertable.Replication.*", strs(),
        "Address of Replication Master (hostname:port) of a cluster")
  ;
}

} // namespace Hypertable
