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
#include "Common/FailureInducer.h"

#include "Context.h"
#include "LoadBalancer.h"
#include "Operation.h"
#include "OperationRecover.h"
#include "OperationBalance.h"
#include "ReferenceManager.h"
#include "RecoveryReplayCounter.h"
#include "BalancePlanAuthority.h"

using namespace Hypertable;
using namespace std;

class RecoverySessionCallback : public Hyperspace::HandleCallback {
  public:
    RecoverySessionCallback(ContextPtr context, String rs)
      : Hyperspace::HandleCallback(Hyperspace::EVENT_MASK_LOCK_RELEASED), 
        m_context(context), m_rs(rs) {
    }

    virtual void lock_released() {
      RangeServerConnectionPtr rsc;
      if (m_context->find_server_by_location(m_rs, rsc)) {
        if (m_context->disconnect_server(rsc)) {
          HT_INFOF("RangeServer %s lost its hyperspace lock; starting recovery",
                rsc->location().c_str());
          OperationPtr operation = new OperationRecover(m_context, rsc);
          m_context->op->add_operation(operation);
        }
      }
    }

  private:
    ContextPtr m_context;
    String m_rs;
};

Context::~Context() {
  if (hyperspace && master_file_handle > 0) {
    hyperspace->close(master_file_handle);
    master_file_handle = 0;
  }
  delete balancer;
  delete reference_manager;
  delete m_balance_plan_authority;
}

void Context::add_server(RangeServerConnectionPtr &rsc) {
  ScopedLock lock(mutex);
  pair<Sequence::iterator, bool> insert_result = m_server_list.push_back( RangeServerConnectionEntry(rsc) );

  if (!insert_result.second) {
    HT_INFOF("Tried to insert %s host=%s local=%s public=%s", rsc->location().c_str(),
             rsc->hostname().c_str(), rsc->local_addr().format().c_str(),
             rsc->public_addr().format().c_str());
    for (Sequence::iterator iter = m_server_list.begin(); iter != m_server_list.end(); ++iter) {
      HT_INFOF("Contains %s host=%s local=%s public=%s", iter->location().c_str(),
               iter->hostname().c_str(), iter->local_addr().format().c_str(),
               iter->public_addr().format().c_str());
    }
    HT_ASSERT(insert_result.second);
  }
}

bool Context::connect_server(RangeServerConnectionPtr &rsc, const String &hostname,
                             InetAddr local_addr, InetAddr public_addr) {
  ScopedLock lock(mutex);
  LocationIndex &hash_index = m_server_list.get<1>();
  LocationIndex::iterator iter;
  PublicAddrIndex &public_addr_index = m_server_list.get<3>();
  PublicAddrIndex::iterator public_addr_iter;

  bool retval = false;
  bool notify = false;

  comm->set_alias(local_addr, public_addr);
  comm->add_proxy(rsc->location(), hostname, public_addr);
  HT_INFOF("Registered proxy %s", rsc->location().c_str());

  if (rsc->connect(hostname, local_addr, public_addr, test_mode)) {
    conn_count++;
    if (conn_count == 1)
      notify = true;
    retval = true;
  }

  if (m_server_list_iter != m_server_list.end() &&
      m_server_list_iter->location() == rsc->location())
    ++m_server_list_iter;

  // Remove this connection if already exists
  iter = hash_index.find(rsc->location());
  if (iter != hash_index.end())
    hash_index.erase(iter);
  public_addr_iter = public_addr_index.find(rsc->public_addr());
  // NOTE: If this assert is still here on 4/1/2013, remove it!
  HT_ASSERT(public_addr_iter == public_addr_index.end());

  // Add it (or re-add it)
  pair<Sequence::iterator, bool> insert_result = m_server_list.push_back(RangeServerConnectionEntry(rsc));
  HT_ASSERT(insert_result.second);
  if (m_server_list.size() == 1 || m_server_list_iter == m_server_list.end())
    m_server_list_iter = m_server_list.begin();

  if (notify)
    cond.notify_all();

  return retval;
}

void Context::register_recovery_callback(RangeServerConnectionPtr &rsc)
{
  ScopedLock lock(mutex);
  String rspath = toplevel_dir + "/servers/" + rsc->location();
  Hyperspace::HandleCallbackPtr cb = new RecoverySessionCallback(this, 
          rsc->location());
  uint64_t handle = hyperspace->open(rspath, Hyperspace::OPEN_FLAG_READ, cb);
  HT_ASSERT(handle);
  m_recovery_state.m_hyperspace_handles.insert(RecoveryState::HandleMap::value_type(rsc->location(), handle));
}

void Context::notification_hook(int type, const String &message)
{
  String type_str;
  switch (type) {
    case NotificationHookType::NOTICE:
      type_str = "NOTICE";
      break;
    case NotificationHookType::ERROR:
      type_str = "ERROR";
      break;
    default:
      type_str = "unknown type";
      break;
  };

  String cmd = format("%s/conf/notification-hook.sh '%s' '%s'", 
          System::install_dir.c_str(), type_str.c_str(), message.c_str());

  HT_DEBUG_OUT << "notification-hook (" << type_str << "): " << cmd << HT_END;

  int ret = ::system(cmd.c_str());
  HT_INFO_OUT << "notification-hook returned: " << ret << HT_END;
  if (ret != 0) {
    HT_WARNF("shell script conf/notification-hook.sh ('%s') returned "
            "error %d", cmd.c_str(), ret);
  }
}

void Context::set_balance_plan_authority(BalancePlanAuthority *bpa) {
  ScopedLock lock(mutex);
  m_balance_plan_authority = bpa;
}

BalancePlanAuthority *Context::get_balance_plan_authority() {
  ScopedLock lock(mutex);
  if (!m_balance_plan_authority)
    m_balance_plan_authority = new BalancePlanAuthority(this, mml_writer);
  return m_balance_plan_authority;
}

void Context::replay_complete(EventPtr &event) {
  int64_t id;
  uint32_t attempt, fragment;
  map<uint32_t, int> error_map;
  const uint8_t *decode_ptr = event->payload;
  size_t decode_remain = event->payload_len;
  int nn, error;
  bool success;

  id       = Serialization::decode_i64(&decode_ptr, &decode_remain);
  attempt  = Serialization::decode_i32(&decode_ptr, &decode_remain);
  nn       = Serialization::decode_i32(&decode_ptr, &decode_remain);

  HT_DEBUG_OUT << "Received replay_complete for op_id=" << id << " attempt="
      << attempt << " num_ranges=" << nn << " from " << event->proxy << HT_END;

  for (int ii = 0; ii < nn; ++ii) {
    fragment = Serialization::decode_i32(&decode_ptr, &decode_remain);
    error    = Serialization::decode_i32(&decode_ptr, &decode_remain);
    error_map[fragment] = error;
  }
  success  = Serialization::decode_bool(&decode_ptr, &decode_remain);

  {
    ScopedLock lock(m_recovery_state.m_mutex);
    RecoveryState::ReplayMap::iterator it = m_recovery_state.m_replay_map.find(id);
    RecoveryReplayCounterPtr replay_counter;
    if (it != m_recovery_state.m_replay_map.end()) {
      replay_counter = it->second;
      if (!replay_counter->complete(attempt, success, error_map)) {
        HT_WARN_OUT << "non-pending player complete message received for "
            "operation=" << id << " attempt=" << attempt << HT_END;
      }
    }
    else {
      HT_WARN_OUT << "No RecoveryReplayCounter found for operation="
          << id << " attempt=" << attempt << HT_END;
    }

    // if there were any corrupt fragments then notify the admin
    bool notify = replay_counter->has_errors();
    RecoveryReplayCounter::ErrorMap errors = replay_counter->get_errors();
    if (HT_FAILURE_SIGNALLED("bad-log-fragments-1")
        && !replay_counter->has_errors()) {
      errors[1] = Error::BAD_SCHEMA;
      errors[2] = Error::BLOCK_COMPRESSOR_DEFLATE_ERROR;
      notify = true;
    }
    if (notify) {
      String msg;
      msg = 
"Dear administrator,\\n"
"\\n"
"The recently started recovery operation reported failures when recovering\\n"
"log file fragments.  The following fragments are corrupt:\\n\\n";
      RecoveryReplayCounter::ErrorMap::iterator it;
      for (it = errors.begin(); it != errors.end(); ++it) {
        if (it->second != Error::OK) {
          msg += format("\t fragment id %u: error 0x%x (%s)\\n",
                  it->first, it->second, Error::get_text(it->second));
          HT_ERRORF("Corrupted logfile fragment %u: error 0x%x (%s)",
                  it->first, it->second, Error::get_text(it->second));
        }
      }
      msg += 
"\\n\\n"
"The log files on the Master and the RangeServer(s) will have more information\\n"
"about the error.\\n\\n";

      notification_hook(NotificationHookType::ERROR, msg);
    }
  }

  HT_DEBUG_OUT << "Exitting replay_complete for op_id=" << id << " attempt="
      << attempt << " num_ranges=" << nn << " from " << event->proxy << HT_END;
}

void Context::prepare_complete(EventPtr &event) {
  const uint8_t *decode_ptr = event->payload;
  size_t decode_remain = event->payload_len;
  int64_t id;
  uint32_t attempt;
  int nn;
  RecoveryCounter::Result rr;
  vector<RecoveryCounter::Result> results;

  id       = Serialization::decode_vi64(&decode_ptr, &decode_remain);
  attempt  = Serialization::decode_vi32(&decode_ptr, &decode_remain);
  nn       = Serialization::decode_vi32(&decode_ptr, &decode_remain);

  HT_DEBUG_OUT << "Received prepare_complete for op_id=" << id << " attempt="
      << attempt << " num_ranges=" << nn << " from " << event->proxy << HT_END;

  for (int ii=0; ii<nn; ++ii) {
    rr.range.decode(&decode_ptr, &decode_remain);
    rr.error    = Serialization::decode_vi32(&decode_ptr, &decode_remain);
    results.push_back(rr);
  }

  {
    ScopedLock lock(m_recovery_state.m_mutex);
    RecoveryState::CounterMap::iterator it = m_recovery_state.m_prepare_map.find(id);
    RecoveryCounterPtr prepare_counter;
    if (it != m_recovery_state.m_prepare_map.end()) {
      prepare_counter = it->second;
      prepare_counter->result_callback(attempt, results);
    }
    else
      HT_WARN_OUT << "No RecoveryCounter found for operation=" << id << HT_END;
  }
  HT_DEBUG_OUT << "Exitting prepare_complete for op_id=" << id << " attempt="
      << attempt << " num_ranges=" << nn << " from " << event->proxy << HT_END;

  return;
}

void Context::commit_complete(EventPtr &event) {
  const uint8_t *decode_ptr = event->payload;
  size_t decode_remain = event->payload_len;
  int64_t id;
  uint32_t attempt;
  int nn;
  RecoveryCounter::Result rr;
  vector<RecoveryCounter::Result> results;

  id       = Serialization::decode_vi64(&decode_ptr, &decode_remain);
  attempt  = Serialization::decode_vi32(&decode_ptr, &decode_remain);
  nn       = Serialization::decode_vi32(&decode_ptr, &decode_remain);
  for (int ii = 0; ii < nn; ++ii) {
    rr.range.decode(&decode_ptr, &decode_remain);
    rr.error    = Serialization::decode_vi32(&decode_ptr, &decode_remain);
    results.push_back(rr);
  }

  HT_DEBUG_OUT << "Received phantom_commit_complete for op_id=" << id
      << " attempt=" << attempt << " num_ranges=" << nn << " from "
      << event->proxy << HT_END;
  {
    ScopedLock lock(m_recovery_state.m_mutex);
    RecoveryState::CounterMap::iterator it = m_recovery_state.m_commit_map.find(id);
    RecoveryCounterPtr commit_counter;
    if (it != m_recovery_state.m_commit_map.end()) {
      commit_counter = it->second;
      commit_counter->result_callback(attempt, results);
    }
    else
      HT_WARN_OUT << "No RecoveryCounter found for operation=" << id
          << HT_END;
  }

  HT_DEBUG_OUT << "Exitting phantom_commit_complete for op_id=" << id
      << " attempt=" << attempt << " num_ranges=" << nn << " from "
      << event->proxy << HT_END;
  return;
}

bool Context::disconnect_server(RangeServerConnectionPtr &rsc) {
  ScopedLock lock(mutex);
  HT_ASSERT(conn_count > 0);

  HT_INFOF("Unregistering proxy %s", rsc->location().c_str());

  RecoveryState::HandleMap::iterator it = m_recovery_state.m_hyperspace_handles.find(rsc->location());
  if (it != m_recovery_state.m_hyperspace_handles.end()) {
    hyperspace->close_nowait((*it).second);
    m_recovery_state.m_hyperspace_handles.erase(it);
  }

  if (rsc->disconnect()) {
    conn_count--;
    return true;
  }

  return false;
}

void Context::wait_for_server() {
  ScopedLock lock(mutex);
  while (conn_count == 0)
    cond.wait(lock);
}


bool Context::find_server_by_location(const String &location, RangeServerConnectionPtr &rsc) {
  ScopedLock lock(mutex);
  LocationIndex &hash_index = m_server_list.get<1>();
  LocationIndex::iterator lookup_iter;

  if ((lookup_iter = hash_index.find(location)) == hash_index.end()) {
    //HT_DEBUG_OUT << "can't find server with location=" << location << HT_END;
    //for (Sequence::iterator iter = m_server_list.begin(); iter != m_server_list.end(); ++iter) {
    //  HT_DEBUGF("Contains %s host=%s local=%s public=%s", iter->location().c_str(),
    //           iter->hostname().c_str(), iter->local_addr().format().c_str(),
    //           iter->public_addr().format().c_str());
    //}
    rsc = 0;
    return false;
  }
  rsc = lookup_iter->rsc;
  return true;
}


bool Context::find_server_by_hostname(const String &hostname, RangeServerConnectionPtr &rsc) {
  ScopedLock lock(mutex);
  HostnameIndex &hash_index = m_server_list.get<2>();

  pair<HostnameIndex::iterator, HostnameIndex::iterator> p = hash_index.equal_range(hostname);
  if (p.first != p.second) {
    rsc = p.first->rsc;
    if (++p.first == p.second)
      return true;
    rsc = 0;
  }
  return false;
}



bool Context::find_server_by_public_addr(InetAddr addr, RangeServerConnectionPtr &rsc) {
  ScopedLock lock(mutex);
  PublicAddrIndex &hash_index = m_server_list.get<3>();
  PublicAddrIndex::iterator lookup_iter;

  if ((lookup_iter = hash_index.find(addr)) == hash_index.end()) {
    rsc = 0;
    return false;
  }
  rsc = lookup_iter->rsc;
  return true;
}


bool Context::find_server_by_local_addr(InetAddr addr, RangeServerConnectionPtr &rsc) {
  ScopedLock lock(mutex);
  LocalAddrIndex &hash_index = m_server_list.get<4>();

  for (pair<LocalAddrIndex::iterator, LocalAddrIndex::iterator> p = hash_index.equal_range(addr);
       p.first != p.second; ++p.first) {
    if (p.first->connected()) {
      rsc = p.first->rsc;
      return true;
    }
  }
  return false;
}

void Context::erase_server(RangeServerConnectionPtr &rsc) {
  ScopedLock lock(mutex);
  LocationIndex &hash_index = m_server_list.get<1>();
  LocationIndex::iterator iter;
  PublicAddrIndex &public_addr_index = m_server_list.get<3>();
  PublicAddrIndex::iterator public_addr_iter;

  // Remove this connection if already exists
  iter = hash_index.find(rsc->location());
  if (iter != hash_index.end())
    hash_index.erase(iter);
  public_addr_iter = public_addr_index.find(rsc->public_addr());
  if (public_addr_iter != public_addr_index.end())
    public_addr_index.erase(public_addr_iter);
  // reset server list iter
  m_server_list_iter = m_server_list.begin();

  // drop server from monitor list
  monitoring->drop_server(rsc->location());

  rsc->set_removed();
}

bool Context::next_available_server(RangeServerConnectionPtr &rsc) {
  ScopedLock lock(mutex);

  if (m_server_list.empty())
    return false;

  if (m_server_list_iter == m_server_list.end())
    m_server_list_iter = m_server_list.begin();

  ServerList::iterator saved_iter = m_server_list_iter;

  do {
    ++m_server_list_iter;
    if (m_server_list_iter == m_server_list.end())
      m_server_list_iter = m_server_list.begin();
    if (m_server_list_iter->rsc->connected()
            && !m_server_list_iter->rsc->is_recovering()) {
      rsc = m_server_list_iter->rsc;
      return true;
    }
  } while (m_server_list_iter != saved_iter);

  return false;
}

bool Context::reassigned(TableIdentifier *table, RangeSpec &range, String &location) {
  // TBD
  return false;
}


bool Context::is_connected(const String &location) {
  RangeServerConnectionPtr rsc;
  if (find_server_by_location(location, rsc))
    rsc->connected();
  return false;
}


void Context::get_servers(std::vector<RangeServerConnectionPtr> &servers) {
  ScopedLock lock(mutex);
  for (ServerList::iterator iter = m_server_list.begin(); iter != m_server_list.end(); ++iter) {
    if (!iter->removed())
      servers.push_back(iter->rsc);
  }
}

size_t Context::connected_server_count() {
  ScopedLock lock(mutex);
  size_t count=0;
  for (ServerList::iterator iter = m_server_list.begin(); iter != m_server_list.end(); ++iter) {
    if (!iter->removed() && iter->connected())
      ++count;
  }
  return count;
}
void Context::get_connected_servers(std::vector<RangeServerConnectionPtr> &servers) {
  ScopedLock lock(mutex);
  for (ServerList::iterator iter = m_server_list.begin(); iter != m_server_list.end(); ++iter) {
    if (!iter->removed() && iter->connected())
      servers.push_back(iter->rsc);
  }
}

void Context::get_connected_servers(StringSet &locations) {
  ScopedLock lock(mutex);
  for (ServerList::iterator iter = m_server_list.begin(); iter != m_server_list.end(); ++iter) {
    if (!iter->removed() && iter->connected())
      locations.insert(iter->location());
  }
}

void Context::get_unbalanced_servers(const std::vector<String> &locations,
    std::vector<RangeServerConnectionPtr> &unbalanced) {
  ScopedLock lock(mutex);
  LocationIndex &hash_index = m_server_list.get<1>();
  LocationIndex::iterator lookup_iter;
  RangeServerConnectionPtr rsc;

  foreach_ht(const String &location, locations) {
    if ((lookup_iter = hash_index.find(location)) == hash_index.end())
      continue;
    rsc = lookup_iter->rsc;
    if (!rsc->get_removed() && !rsc->get_balanced())
      unbalanced.push_back(rsc);
  }
}

void Context::set_servers_balanced(const std::vector<RangeServerConnectionPtr> &unbalanced) {
  ScopedLock lock(mutex);
  foreach_ht (const RangeServerConnectionPtr rsc, unbalanced) {
    rsc->set_balanced();
  }
}

bool Context::can_accept_ranges(const RangeServerStatistics &stats)
{
  static int threshold = 0;
  if (!threshold)
    threshold = props->get_i32("Hypertable.Master.DiskThreshold.Percentage");

  // system info was not yet initialized; assume that the disks are available
  if (!stats.stats) {
    HT_WARNF("RangeServer %s: no disk usage statistics available",
            stats.location.c_str());
    return true;
  }

  // accept new ranges if there's at least one disk below the threshold
  foreach_ht (const FsStat &fs, stats.stats->system.fs_stat) {
    if (fs.use_pct < threshold)
      return true;
  }
  HT_WARNF("RangeServer %s: all disks are above threshold of %d %% "
          "(Hypertable.Master.DiskThresholdPct); will not assign ranges",
          stats.location.c_str(), threshold);
  return false;
}

RecoveryReplayCounterPtr
Context::RecoveryState::create_replay_counter(int64_t id, uint32_t attempt)
{
  ScopedLock lock(m_mutex);

  RecoveryReplayCounterPtr counter = new RecoveryReplayCounter(attempt);

  if (m_replay_map.find(id) != m_replay_map.end())
    m_replay_map.erase(id);

  m_replay_map.insert(make_pair(id, counter));
  return counter;
}

void 
Context::RecoveryState::erase_replay_counter(int64_t id)
{
  ScopedLock lock(m_mutex);
  m_replay_map.erase(id);
}

RecoveryCounterPtr
Context::RecoveryState::create_prepare_counter(int64_t id, uint32_t attempt)
{
  ScopedLock lock(m_mutex);

  RecoveryCounterPtr counter = new RecoveryCounter(attempt);

  if (m_prepare_map.find(id) != m_prepare_map.end())
    m_prepare_map.erase(id);

  m_prepare_map.insert(make_pair(id, counter));
  return counter;
}

void
Context::RecoveryState::erase_prepare_counter(int64_t id)
{
  ScopedLock lock(m_mutex);
  m_prepare_map.erase(id);
}

RecoveryCounterPtr
Context::RecoveryState::create_commit_counter(int64_t id, uint32_t attempt)
{
  ScopedLock lock(m_mutex);

  RecoveryCounterPtr counter = new RecoveryCounter(attempt);

  if (m_commit_map.find(id) != m_commit_map.end())
    m_commit_map.erase(id);

  m_commit_map.insert(make_pair(id, counter));
  return counter;
}

void
Context::RecoveryState::erase_commit_counter(int64_t id)
{
  ScopedLock lock(m_mutex);
  m_commit_map.erase(id);
}
