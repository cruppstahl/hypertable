/**
 * Copyright (C) 2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or any later version.
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
#include <iostream>
#include <map>
#include <cassert>
#include "Hypertable/Lib/Client.h"
#include "Hypertable/Lib/ClusterConfiguration.h"
#include "Hypertable/Lib/ClusterIdManager.h"

using namespace Hypertable;

int 
main(int argc, char **argv)
{
  ClusterConfiguration *cc;
  ClusterIdManager *id;
  Client *ht_client = new Client(argv[0], "./cluster_test.cfg");

  cc = new ClusterConfiguration(ht_client->get_properties());
  id = new ClusterIdManager(ht_client->get_hyperspace_session(),
          ht_client->get_properties());

  // assign two IDs and make sure that they are unique
  uint64_t id1, id2;
  id->assign_new_local_id();
  id1 = id->get_local_id();
  sleep(2);
  id->assign_new_local_id();
  id2 = id->get_local_id();
  assert(id1 != id2);
  delete id;

  // reload the ClusterIdManager; make sure that the last id was stored
  // and retrieved correctly
  id = new ClusterIdManager(ht_client->get_hyperspace_session(),
          ht_client->get_properties());
  assert(id2 == id->get_local_id());
  delete id;
  id = 0;

  ClusterConfiguration::RemoteMasterMap map = cc->get_all_clusters();
  ClusterConfiguration::RemoteMasterMap::iterator it;
  for (it = map.begin(); it != map.end(); ++it) {
    cout << (*it).first << ", " << (*it).second << std::endl;
  }

  // this is the list of remote masters; make sure they're all found
  //   cluster1-east, 107.21.38.98:38100
  //   cluster1-east, 127.0.0.1:12345
  //   cluster1-west, 107.21.38.98:444
  //   cluster1-west, 107.21.38.98:38100
  //   cluster1-west, 127.0.0.1:38100
  std::vector<InetAddr> addr;
  addr = cc->get_cluster_addresses("cluster1-east");
  assert(addr.size() == 2);
  assert(addr[0] == InetAddr("107.21.38.98", 38100));
  assert(addr[1] == InetAddr("127.0.0.1", 12345));

  addr = cc->get_cluster_addresses("cluster1-west");
  assert(addr.size() == 3);
  assert(addr[0] == InetAddr("107.21.38.98", 444));
  assert(addr[1] == InetAddr("107.21.38.98", 38100));
  assert(addr[2] == InetAddr("127.0.0.1", 38100));

  addr = cc->get_cluster_addresses("cluster1-unknown");
  assert(addr.size() == 0);
  
  delete cc;
  delete ht_client;
  return (0);
}
