/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
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

#ifndef HYPERTABLE_SERVERUP_H
#define HYPERTABLE_SERVERUP_H

#include "Common/String.h"

#include "AsyncComm/ConnectionManager.h"

#include <cstdlib>
#include <map>
#include <boost/function/function2.hpp>

namespace Hypertable {

typedef boost::function2<void, ConnectionManagerPtr &, uint32_t>
    CheckerFunction;

typedef std::map<String, CheckerFunction>
    CheckerMap;

extern void wait_for_connection(const char *server,
        ConnectionManagerPtr &conn_mgr, InetAddr &addr,
        int timeout_ms, int wait_ms);

extern CheckerMap global_map;

extern bool verbose;

} // namespace Hypertable

#endif // HYPERTABLE_SERVERUP_H
