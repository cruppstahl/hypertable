/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License.
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
#include "Common/InetAddr.h"
#include "Common/ScopeGuard.h"
#include "Common/md5.h"

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include "ClusterConfiguration.h"

#include <vector>

using namespace boost;
using namespace Hyperspace;


namespace Hypertable {

ClusterConfiguration::ClusterConfiguration(PropertiesPtr &props)
  : m_properties(props) {
  read_from_configuration();
}

bool
ClusterConfiguration::has_remote_cluster(const String &alias) {
  ScopedLock lock(m_mutex);
  
  std::pair<ClusterConfiguration::RemoteMasterMap::iterator,
      ClusterConfiguration::RemoteMasterMap::iterator> it
          = m_clusters.equal_range(alias);
  return (it.first != it.second);
}

std::vector<InetAddr>
ClusterConfiguration::get_cluster_addresses(const String &alias) {
  ScopedLock lock(m_mutex);
  
  std::vector<InetAddr> ret;
  std::pair<ClusterConfiguration::RemoteMasterMap::iterator,
      ClusterConfiguration::RemoteMasterMap::iterator> it;
  for (it = m_clusters.equal_range(alias); it.first != it.second; ++it.first)
    ret.push_back(it.first->second);
  return ret;
}

ClusterConfiguration::RemoteMasterMap
ClusterConfiguration::get_all_clusters() {
  ScopedLock lock(m_mutex);
  return m_clusters;
}

void 
ClusterConfiguration::read_from_configuration() {
  // no need to lock; this function is called from the constructor
  std::vector<String> names;
  m_properties->get_names(names);

  // this is how the format looks like:
  // Hypertable.Replication.Cluster.vsnl-primary.Master = 234.1.2.3:port
  // Hypertable.Replication.Cluster.vsnl-primary.Master = 231.5.6.7:12345

  for (size_t i = 0; i < names.size(); i++) {
    String alias;
    if (starts_with(names[i], "Hypertable.Replication.Cluster.")) {
      const char *p = strchr(names[i].c_str() + 31, '.');
      if (!p) {
        HT_ERRORF("Ignoring invalid cluster alias %s", names[i].c_str());
        continue;
      }
      alias = String(names[i].c_str() + 31, p);

      if (strstr(p, ".Master")) {
        // split the value into <hostname>:<port>; if port is not specified: 
        // use the default port
        Strings values = m_properties->get_strs(names[i]);
        foreach_ht (String &val, values) {
          unsigned short port;
          String address;
          p = strchr((char *)val.c_str(), ':');
          if (!p) {
            address = val;
            port = m_properties->get_i16("Hypertable.Replication.Master.Port");
          }
          else {
            address = String(val.c_str(), p);
            port = (unsigned short)strtol(p + 1, 0, 0);
            if (!port) {
              HT_ERRORF("Ignoring invalid cluster alias %s", names[i].c_str());
              continue;
            }
          }
          // now store the cluster information in our list
          m_clusters.insert(RemoteMasterMap::value_type(alias,
                      InetAddr(address, port)));
        }
      }
      else {
        HT_ERRORF("Ignoring invalid cluster alias %s", names[i].c_str());
        continue;
      }
    }
  }
}

} // namespace Hypertable
