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

#ifndef HYPERTABLE_CLUSTERCONFIGURATION_H
#define HYPERTABLE_CLUSTERCONFIGURATION_H

#include "Common/InetAddr.h"
#include "Common/Properties.h"
#include "Hyperspace/Session.h"

#include <vector>
#include <map>

namespace Hypertable {

/**
 * Manages cluster configuration in the configuration files
 */
class ClusterConfiguration : public ReferenceCount {
  public:
    typedef std::multimap<String, InetAddr> RemoteMasterMap;

    /**
     * constructor; reads the cluster information from Hyperspace and
     * from the configuration file
     */
    ClusterConfiguration(PropertiesPtr &props);

    /**
     * checks if a remote cluster exists
     */
    bool has_remote_cluster(const String &alias);

    /**
     * retrieves the internet addresses of all remote replication masters
     * of a cluster
     */
    std::vector<InetAddr> get_cluster_addresses(const String &alias);

    /**
     * retrieves a list of all known clusters
     */
    RemoteMasterMap get_all_clusters();

  private:
    /** reads the remote IDs from the configuration file */
    void read_from_configuration();

    /** a mutex to serialize access */
    Mutex m_mutex;

    /** the configuration properties */
    PropertiesPtr m_properties;

    /** all known remote masters */
    RemoteMasterMap m_clusters;
};

typedef intrusive_ptr<ClusterConfiguration> ClusterConfigurationPtr;

} // namespace Hypertable

#endif // HYPERTABLE_CLUSTERCONFIGURATION_H
