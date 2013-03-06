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

#ifndef REPLICATION_TYPES_H
#define REPLICATION_TYPES_H

#include "Common/String.h"
#include "Common/InetAddr.h"

namespace Hypertable { namespace ReplicationTypes {

// maps a fragment path (in the DFS) to a remote cluster
typedef std::map<String, String> FragmentToClusterMap;

// maps a list of slaves to a cluster
typedef std::map<String, std::vector<InetAddr> > ClusterToSlavesMap;

// maps a table id to remote cluster(s)
typedef std::map<String, std::vector<String> > TableIdToClusterMap;

// maps a table id to a table name
typedef std::map<String, String> TableIdToTableNameMap;

} } // namespace Hypertable::ReplicationTypes

#endif // REPLICATION_TYPES_H
