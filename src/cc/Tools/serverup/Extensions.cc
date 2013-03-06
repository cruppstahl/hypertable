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
#include "Common/Compat.h"

extern "C" {
#include <netdb.h>
#include <sys/types.h>
#include <signal.h>
}

#include "Common/InetAddr.h"
#include "Common/Config.h"
#include "Common/Init.h"
#include "Common/FileUtils.h"

#include "AsyncComm/DispatchHandlerSynchronizer.h"

#include "Hypertable/Lib/ReplicationMasterProtocol.h"
#include "Hypertable/Lib/ReplicationSlaveProtocol.h"

#include "serverup.h"
#include "Extensions.h"

namespace Hypertable {

using namespace Config;

static void
check_repmaster(ConnectionManagerPtr &conn_mgr, uint32_t wait_ms) {
  if (properties->has("host"))
    properties->set("repmaster-host",
            properties->get_str("host"));
  else
    properties->set("repmaster-host",
            String("localhost"));

  if (properties->has("Hypertable.Replication.Master.Port"))
    properties->set("repmaster-port",
            properties->get_i16("Hypertable.Replication.Master.Port"));
  else
    properties->set("repmaster-port",
            properties->get_i16("Hypertable.Replication.Master.Port"));

  if (get_bool("display-address")) {
    std::cout << get_str("repmaster-host") << ":"
        << get_i16("repmaster-port") << std::endl;
    _exit(0);
  }

  InetAddr addr(get_str("repmaster-host"), get_i16("repmaster-port"));

  conn_mgr->add(addr, wait_ms, "Replication.Master");

  DispatchHandlerSynchronizer sync_handler;
  EventPtr event;
  CommBufPtr cbp(ReplicationMasterProtocol::create_status_request());

  int error;
  if (!(error = Comm::instance()->send_request(addr, wait_ms, cbp,
      &sync_handler))) {
    if (sync_handler.wait_for_reply(event))
      return;
  }

  // if connection times out: check if the process is running; this might
  // be a second master waiting to get its hyperspace lock
  String pidstr;
  String pid_file = System::install_dir + "/run/Replication.Master.pid";
  if (FileUtils::read(pid_file, pidstr) <= 0)
    HT_THROW(Error::REQUEST_TIMEOUT, "connecting to master");
  pid_t pid = (pid_t)strtoul(pidstr.c_str(), 0, 0);
  if (pid <= 0)
    HT_THROW(Error::REQUEST_TIMEOUT, "connecting to master");
  // (kill(pid, 0) does not send any signal but checks if the process exists
  if (::kill(pid, 0) < 0)
    HT_THROW(Error::REQUEST_TIMEOUT, "connecting to master");
}

static void
check_repslave(ConnectionManagerPtr &conn_mgr, uint32_t wait_ms) {
  if (properties->has("host"))
    properties->set("repslave-host", properties->get_str("host"));
  else
    properties->set("repslave-host", String("localhost"));

  if (properties->has("Hypertable.Replication.Slave.Port"))
    properties->set("repslave-port",
            properties->get_i16("Hypertable.Replication.Slave.Port"));
  else
    properties->set("repslave-port",
            properties->get_i16("Hypertable.Replication.Slave.Port"));

  if (get_bool("display-address")) {
    std::cout << get_str("repslave-host") << ":"
        << get_i16("repslave-port") << std::endl;
    _exit(0);
  }

  InetAddr addr(get_str("repslave-host"), get_i16("repslave-port"));

  wait_for_connection("replication slave", conn_mgr, addr, wait_ms, wait_ms);

  DispatchHandlerSynchronizer sync_handler;
  EventPtr event;
  CommBufPtr cbp(ReplicationSlaveProtocol::create_status_request());

  int error;
  if ((error = Comm::instance()->send_request(addr, wait_ms, cbp,
      &sync_handler)))
    HT_THROW(error, String("Comm::send_request failure: ")
            + Error::get_text(error));

  if (!sync_handler.wait_for_reply(event))
    HT_THROW((int)Protocol::response_code(event),
            String("Replication.Slave status() failure: ")
            + Protocol::string_format_message(event));
}

void
Extensions::add_checkers(CheckerMap &checker_map) {
  checker_map["Replication.Master"] = check_repmaster;
  checker_map["repmaster"] = check_repmaster;
  checker_map["Replication.Slave"] = check_repslave;
  checker_map["repslave"] = check_repslave;
}

} // namespace Hypertable

