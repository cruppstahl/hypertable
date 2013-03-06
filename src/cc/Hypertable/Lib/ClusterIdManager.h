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

#ifndef HYPERTABLE_CLUSTERIDMANAGER_H
#define HYPERTABLE_CLUSTERIDMANAGER_H

#include "Common/InetAddr.h"
#include "Common/Properties.h"
#include "Hyperspace/Session.h"

#include <vector>
#include <map>

namespace Hypertable {

/**
 * Retrieves or assigns the local cluster ID
 */
class ClusterIdManager : public ReferenceCount {
  public:
    /**
     * constructor; reads the cluster information from Hyperspace
     */
    ClusterIdManager(Hyperspace::SessionPtr &hyperspace, PropertiesPtr &props);

    /**
     * retrieves the cluster ID of the local cluster; returns 0 if no ID
     * was assigned
     */
    uint64_t get_local_id() {
      ScopedLock lock(m_mutex);
      return m_local_id;
    }

    /**
     * creates and stores a new ID for the local cluster; an existing ID is
     * overwritten.
     * returns the new Id
     */
    uint64_t assign_new_local_id();

  private:
    /** reads the local ID from hyperspace */
    void read_from_hyperspace();

    /** a mutex to serialize access */
    Mutex m_mutex;

    /** the hyperspace session */
    Hyperspace::SessionPtr m_hyperspace;

    /** the configuration properties */
    PropertiesPtr m_properties;

    /** the local cluster ID */
    uint64_t m_local_id;
};

typedef intrusive_ptr<ClusterIdManager> ClusterIdManagerPtr;

} // namespace Hypertable

#endif // HYPERTABLE_CLUSTERIDMANAGER_H
