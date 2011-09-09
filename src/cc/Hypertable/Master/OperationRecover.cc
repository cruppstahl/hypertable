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

#include "Common/Compat.h"
#include "Common/Error.h"
#include "Common/md5.h"
#include "Common/FailureInducer.h"

#include "OperationRecover.h"
#include "OperationRecoverRanges.h"
#include "Hypertable/Lib/MetaLogReader.h"
#include "Hypertable/Lib/MetaLogEntityRange.h"
#include "Hypertable/RangeServer/MetaLogDefinitionRangeServer.h"
#include "BalancePlanAuthority.h"

using namespace Hypertable;
using namespace Hyperspace;

OperationRecover::OperationRecover(ContextPtr &context, 
        RangeServerConnectionPtr &rsc)
  : Operation(context, MetaLog::EntityType::OPERATION_RECOVER_SERVER),
    m_location(rsc->location()), m_rsc(rsc), m_hyperspace_handle(0), 
    m_waiting(false), m_servers_down(0) {
  m_dependencies.insert(Dependency::RECOVERY_BLOCKER);
  m_exclusivities.insert(m_rsc->location());
  m_obstructions.insert(Dependency::RECOVER_SERVER);
  m_hash_code = md5_hash("RecoverServer") ^ md5_hash(m_rsc->location().c_str());
  HT_ASSERT(m_rsc != 0);
  m_rsc->set_recovering(true);
}

OperationRecover::OperationRecover(ContextPtr &context,
    const MetaLog::EntityHeader &header_)
  : Operation(context, header_), m_servers_down(0) {
}

void OperationRecover::notification_hook() {
  // Recovery will only continue if 40% of the RangeServers are running. This
  // setting can be overwritten with the parameter --Hypertable.Failover.Quorum
  StringSet active_locations;
  m_context->get_connected_servers(active_locations);
  size_t quorum_percent =
          m_context->props->get_i32("Hypertable.Failover.Quorum.Percentage");
  size_t servers_total = m_context->server_count();
  size_t servers_required = (servers_total * quorum_percent) / 100;
  size_t servers_up = active_locations.size();
  size_t servers_down = servers_total - servers_up;
  if (servers_required == 0)
    servers_required = 1;

  HT_ASSERT(m_rsc != 0);

  String msg = format(
"Dear administrator,\\n"
"\\n"
"The RangeServer %s (%s) is no longer available and is\\n"
"about to be recovered. All ranges of %s are moved to other machines.\\n"
"After you fixed the failing node please manually delete the file\\n"
"\"run/location\" in the Hypertable directory on %s (usually\\n"
"/opt/hypertable/<version>) before restarting Hypertable on this node.\\n"
"\\n"
"Current statistics:\\n"
"\\n"
"%u server(s) total\\n"
"%u server(s) up\\n"
"%u server(s) down\\n"
"\\n"
"Recovery will only continue if at least %u RangeServers (%u%%) are running.\\n"
"This setting can be overwritten with the parameter --Hypertable.Failover.Quorum.\\n"
"\\n"
"The file Hypertable.RangeServer.log on %s has information why\\n"
"the RangeServer had to be recovered.\\n"
"\\n", 
    m_rsc->location().c_str(), 
    m_rsc->hostname().c_str(),
    m_rsc->location().c_str(),
    m_rsc->hostname().c_str(),
    (unsigned)servers_total, 
    (unsigned)servers_up, 
    (unsigned)servers_down, 
    (unsigned)servers_required, 
    (unsigned)quorum_percent,
    m_rsc->hostname().c_str());

  m_context->notification_hook(NotificationHookType::NOTICE, msg);
}

void OperationRecover::notification_hook_failure(Exception &e) {
  HT_ASSERT(m_rsc != 0);

  String msg = format(
"Dear administrator,\\n"
"\\n"
"The RangeServer %s (%s) is no longer available, but there were errors\\n"
"during recovery and therefore manual intervention is required.\\n"
"\\n"
"The errors are:\\n"
"Error code: %u\\n"
"Error text: %s\\n"
"\\n"
"The files Hypertable.Master.log and Hypertable.RangeServer.log on %s have\\n"
"more information about the failures.\\n"
"\\n", 
    m_rsc->location().c_str(), 
    m_rsc->hostname().c_str(),
    e.code(), e.what(),
    m_rsc->hostname().c_str());

  m_context->notification_hook(NotificationHookType::ERROR, msg);
}

void OperationRecover::execute() {
  int state = get_state();
  int type;

  HT_INFOF("Entering RecoverServer %s state=%s this=%p",
           m_location.c_str(), OperationState::get_text(state), (void *)this);
  if (!m_rsc)
    (void)m_context->find_server_by_location(m_location, m_rsc);
  else
    HT_ASSERT(m_location == m_rsc->location());

  std::vector<Entity *> entities;
  Operation *sub_op;

  if (!m_hyperspace_handle) {
    try {
      // get the hyperspace lock
      if (acquire_server_lock()) {
        // operation blocked till we have waited long enough to know 
        // server is dead
        if (m_waiting)
          return;
      }
      else {
        // rangeserver is connected, no need for recovery
        m_rsc->set_recovering(false);
        complete_ok();
        return;
      }
    }
    catch (Exception &e) {
      HT_ASSERT(state == OperationState::INITIAL);
      // range server is connected to Hyperspace but not to master
      HT_ERROR_OUT << e << HT_END;
      notification_hook_failure(e);
      complete_error(e);
      return;
    }
  }

  switch (state) {
  case OperationState::INITIAL:
    // use an external script to inform the administrator about the recovery
    notification_hook();

    // read rsml figure out what types of ranges lived on this server
    // and populate the various vectors of ranges
    read_rsml();

    // now create a new recovery plan
    create_recovery_plan();

    set_state(OperationState::ISSUE_REQUESTS);
    m_rsc->set_removed();
    HT_MAYBE_FAIL("recover-server-1");
    m_context->mml_writer->record_state(this);
    HT_MAYBE_FAIL("recover-server-2");
    break;

  case OperationState::ISSUE_REQUESTS:
    if (m_root_range.size()) {
      type = RangeSpec::ROOT;
      sub_op = new OperationRecoverRanges(m_context, m_location, type,
                                                m_root_range);
      HT_INFO_OUT << "Number of root ranges to recover for location " 
          << m_location << "="
          << m_root_range.size() << HT_END;
      {
        ScopedLock lock(m_mutex);
        m_dependencies.insert(Dependency::ROOT);
      }
      m_sub_ops.push_back(sub_op);
      entities.push_back(sub_op);
    }
    if (m_metadata_ranges.size()) {
      type = RangeSpec::METADATA;
      sub_op = new OperationRecoverRanges(m_context, m_location, type,
                                                m_metadata_ranges);
      HT_INFO_OUT << "Number of metadata ranges to recover for location "
          << m_location << "="
          << m_metadata_ranges.size() << HT_END;
      {
        ScopedLock lock(m_mutex);
        m_dependencies.insert(Dependency::METADATA);
      }
      m_sub_ops.push_back(sub_op);
      entities.push_back(sub_op);
    }
    if (m_system_ranges.size()) {
      type = RangeSpec::SYSTEM;
      sub_op = new OperationRecoverRanges(m_context, m_location, type,
                                                m_system_ranges);
      HT_INFO_OUT << "Number of system ranges to recover for location "
          << m_location << "="
          << m_system_ranges.size() << HT_END;
      {
        ScopedLock lock(m_mutex);
        m_dependencies.insert(Dependency::SYSTEM);
      }
      m_sub_ops.push_back(sub_op);
      entities.push_back(sub_op);
    }
    if (m_user_ranges.size()) {
      type = RangeSpec::USER;
      sub_op = new OperationRecoverRanges(m_context, m_location, type,
                                                m_user_ranges);
      HT_INFO_OUT << "Number of user ranges to recover for location " 
          << m_location << "="
          << m_user_ranges.size() << HT_END;
      {
        ScopedLock lock(m_mutex);
        m_dependencies.insert(format("%s-user", m_location.c_str()));
      }
      m_sub_ops.push_back(sub_op);
      entities.push_back(sub_op);
    }
    set_state(OperationState::FINALIZE);
    entities.push_back(this);
    HT_DEBUG_OUT << "added " << entities.size() << " sub_ops" << HT_END;
    m_context->mml_writer->record_state(entities);
    HT_MAYBE_FAIL("recover-server-3");
    break;

  case OperationState::FINALIZE:
    // Once recovery is complete, the master blows away the RSML and CL for the
    // server being recovered then it unlocks the hyperspace file
    clear_server_state();
    HT_MAYBE_FAIL("recover-server-5");
    complete_ok();
    HT_MAYBE_FAIL("recover-server-6");
    break;

  default:
    HT_FATALF("Unrecognized state %d", state);
    break;
  }

  HT_INFOF("Leaving RecoverServer %s state=%s this=%p",
           m_location.c_str(), OperationState::get_text(get_state()), 
           (void *)this);
}

OperationRecover::~OperationRecover() {
}

bool OperationRecover::proceed_with_recovery() {
  // do not continue with recovery if the RangeServer is back online
  if (m_rsc->connected())
    return false;

  uint64_t wait_interval = (uint64_t)m_context->props->get_i32("Hypertable.Failover.GracePeriod");

  boost::xtime now;
  boost::xtime_get(&now, boost::TIME_UTC);

  size_t current_servers_down = m_context->server_count() 
            - m_context->connected_server_count();

  // restart waiting if another RangeServer goes down during the grace period
  if (!m_waiting || (m_servers_down < current_servers_down)) {
    HT_INFO_OUT << m_location << ": Currently " << current_servers_down 
        << " servers down (previously " << m_servers_down 
        << "). Waiting for grace period..." << HT_END;
    m_dhp = new DispatchHandlerTimedUnblock(m_context, m_location);
    boost::xtime_get(&m_wait_start, boost::TIME_UTC);
    m_waiting = true;
    m_servers_down = current_servers_down;
    m_context->comm->set_timer(wait_interval, m_dhp.get());
    block();
  }
  else {
    int64_t elapsed_time = xtime_diff_millis(m_wait_start, now);
    if (elapsed_time > 0 && (uint64_t)elapsed_time > wait_interval) {
      m_waiting = false;
      return !m_rsc->connected();
    }
  }
  return true;
}

bool OperationRecover::acquire_server_lock() {

  uint32_t oflags = OPEN_FLAG_READ | OPEN_FLAG_WRITE | OPEN_FLAG_LOCK;
  uint32_t lock_status = LOCK_STATUS_BUSY;
  uint32_t retry_interval = m_context->props->get_i32("Hypertable.Connection.Retry.Interval");
  LockSequencer sequencer;
  bool reported = false;
  int max_retries = 10;
  int retry_count = 0;

  // need to wait for long enough to be certain that the RS has failed
  // before trying to acquire lock
  if (get_state() == OperationState::INITIAL) {
    if (!proceed_with_recovery())
      return false;
    else if (m_waiting)
      return true;
  }

  m_hyperspace_handle =
    m_context->hyperspace->open(m_context->toplevel_dir 
                                + "/servers/" 
                                + m_location,
                                oflags);
  while (lock_status != LOCK_STATUS_GRANTED) {
    m_context->hyperspace->try_lock(m_hyperspace_handle, 
                                LOCK_MODE_EXCLUSIVE, &lock_status,
        &sequencer);
    if (lock_status != LOCK_STATUS_GRANTED) {
      if (!reported) {
        HT_INFO_OUT << "Couldn't obtain lock on '" << m_context->toplevel_dir
          << "/servers/"<< m_location << "' due to conflict, "
          << "entering retry loop ..." << HT_END;
        reported = true;
      }
      if (retry_count > max_retries) {
        HT_THROW(Error::HYPERSPACE_LOCK_CONFLICT, (String)"Couldn't obtain "
                "lock on '" + m_context->toplevel_dir 
                + "/servers/" + m_location +
                "' due to conflict,  hit max_retries " + retry_count);
      }
      poll(0, 0, retry_interval);
      ++retry_count;
    }
  }
  m_context->hyperspace->attr_set(m_hyperspace_handle, "removed", "", 0);
  HT_INFO_OUT << "Obtained lock and set removed attr on " 
      << m_context->toplevel_dir
      << "/servers/" << m_location << HT_END;
  return true;
}

void OperationRecover::display_state(std::ostream &os) {
  os << " location=" << m_location << " ";
}

const String OperationRecover::name() {
  return label();
}

const String OperationRecover::label() {
  return format("RecoverServer %s", m_location.c_str());
}

void OperationRecover::clear_server_state() {
  // remove this RangeServerConnection entry
  //
  // if m_rsc is NULL then it was already removed
  if (m_rsc) {
    HT_INFO_OUT << "delete RangeServerConnection from mml for "
        << m_location << HT_END;
    m_context->mml_writer->record_removal(m_rsc.get());
    m_context->erase_server(m_rsc);
    HT_MAYBE_FAIL("recover-server-4");
  }
  // unlock hyperspace file
  Hyperspace::close_handle_ptr(m_context->hyperspace, &m_hyperspace_handle);
  // delete balance plan
  BalancePlanAuthority *plan = m_context->get_balance_plan_authority();
  plan->remove_recovery_plan(m_location);
}

void OperationRecover::create_recovery_plan() {
  BalancePlanAuthority *plan = m_context->get_balance_plan_authority();
  plan->create_recovery_plan(m_location, m_root_range, m_metadata_ranges,
          m_system_ranges, m_user_ranges);
}

void OperationRecover::read_rsml() {
  // move rsml and commit log to some recovered dir
  MetaLog::DefinitionPtr rsml_definition
      = new MetaLog::DefinitionRangeServer(m_location.c_str());
  MetaLog::ReaderPtr rsml_reader;
  MetaLog::EntityRange *range;
  vector<MetaLog::EntityPtr> entities;
  String logfile;

  try {
    logfile = m_context->toplevel_dir + "/servers/" + m_location + "/log/"
        + rsml_definition->name();
    rsml_reader = new MetaLog::Reader(m_context->dfs, rsml_definition, logfile);
    rsml_reader->get_entities(entities);
    foreach (MetaLog::EntityPtr &entity, entities) {
      if ((range = dynamic_cast<MetaLog::EntityRange *>(entity.get())) != 0) {
        QualifiedRangeStateSpec qrss;
        // skip phantom ranges, let whoever was recovering them deal with them
        if (!(range->state.state & RangeState::PHANTOM)) {
          HT_INFO_OUT << "Range " << *range << ": no PHANTOM; including" << HT_END;
          qrss.qualified_range.range = range->spec;
          qrss.qualified_range.table = range->table;
          qrss.state = range->state;
          if (qrss.qualified_range.is_root())
            m_root_range.push_back(qrss);
          else if (qrss.qualified_range.table.is_metadata())
            m_metadata_ranges.push_back(qrss);
          else if (qrss.qualified_range.table.is_system())
            m_system_ranges.push_back(qrss);
          else
            m_user_ranges.push_back(qrss);
        }
        else
          HT_INFO_OUT << "Range " << *range << ": PHANTOM; skipping" << HT_END;
      }
    }
  }
  catch (Exception &e) {
    HT_FATAL_OUT << e << HT_END;
  }
}

size_t OperationRecover::encoded_state_length() const {
  size_t len = Serialization::encoded_length_vstr(m_location) + 17;
  foreach(const QualifiedRangeStateSpecManaged &range, m_root_range)
    len += range.encoded_length();
  foreach(const QualifiedRangeStateSpecManaged &range, m_metadata_ranges)
    len += range.encoded_length();
  foreach(const QualifiedRangeStateSpecManaged &range, m_system_ranges)
    len += range.encoded_length();
  foreach(const QualifiedRangeStateSpecManaged &range, m_user_ranges)
    len += range.encoded_length();
  return len;
}

void OperationRecover::encode_state(uint8_t **bufp) const {
  Serialization::encode_vstr(bufp, m_location);
  Serialization::encode_bool(bufp, m_waiting);
  Serialization::encode_i32(bufp, m_root_range.size());
  foreach(const QualifiedRangeStateSpecManaged &range, m_root_range)
    range.encode(bufp);
  Serialization::encode_i32(bufp, m_metadata_ranges.size());
  foreach(const QualifiedRangeStateSpecManaged &range, m_metadata_ranges)
    range.encode(bufp);
  Serialization::encode_i32(bufp, m_system_ranges.size());
  foreach(const QualifiedRangeStateSpecManaged &range, m_system_ranges)
    range.encode(bufp);
  Serialization::encode_i32(bufp, m_user_ranges.size());
  foreach(const QualifiedRangeStateSpecManaged &range, m_user_ranges)
    range.encode(bufp);
}

void OperationRecover::decode_state(const uint8_t **bufp,
        size_t *remainp) {
  decode_request(bufp, remainp);
}

void OperationRecover::decode_request(const uint8_t **bufp,
        size_t *remainp) {

  m_location = Serialization::decode_vstr(bufp, remainp);
  m_waiting = Serialization::decode_bool(bufp, remainp);
  boost::xtime_get(&m_wait_start, boost::TIME_UTC);
  int nn;
  QualifiedRangeStateSpec qrss;
  nn = Serialization::decode_i32(bufp, remainp);
  for (int ii = 0; ii < nn; ++ii) {
    qrss.decode(bufp, remainp);
    m_root_range.push_back(qrss);
  }
  nn = Serialization::decode_i32(bufp, remainp);
  for (int ii = 0; ii < nn; ++ii) {
    qrss.decode(bufp, remainp);
    m_metadata_ranges.push_back(qrss);
  }
  nn = Serialization::decode_i32(bufp, remainp);
  for (int ii = 0; ii < nn; ++ii) {
    qrss.decode(bufp, remainp);
    m_system_ranges.push_back(qrss);
  }
  nn = Serialization::decode_i32(bufp, remainp);
  for (int ii = 0; ii < nn; ++ii) {
    qrss.decode(bufp, remainp);
    m_user_ranges.push_back(qrss);
  }
  m_rsc = 0;
  m_hyperspace_handle = 0;
}

