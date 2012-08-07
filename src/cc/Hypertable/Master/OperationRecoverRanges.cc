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

#include "Hypertable/Lib/CommitLogReader.h"

#include "RecoveryReplayCounter.h"
#include "RecoveryCounter.h"
#include "OperationRecoverRanges.h"
#include "OperationRecoveryBlocker.h"
#include "OperationProcessor.h"
#include "BalancePlanAuthority.h"

using namespace Hypertable;

OperationRecoverRanges::OperationRecoverRanges(ContextPtr &context,
        const String &location, int type,
        vector<QualifiedRangeStateSpecManaged> &ranges)
  : Operation(context, MetaLog::EntityType::OPERATION_RECOVER_SERVER_RANGES),
    m_location(location), m_type(type), m_attempt(0), m_ranges(ranges),
    m_plan_generation(0) {
  HT_ASSERT(type != RangeSpec::UNKNOWN);
  set_type_str();
  m_timeout = m_context->props->get_i32("Hypertable.Failover.Timeout");
  m_dependencies.insert(Dependency::RECOVERY_BLOCKER);
  initialize_obstructions_dependencies();
}

OperationRecoverRanges::OperationRecoverRanges(ContextPtr &context,
        const MetaLog::EntityHeader &header_) : Operation(context, header_) {
}

void OperationRecoverRanges::execute() {
  int state = get_state();
  bool initial_done = false;
  bool issue_done = false;
  bool prepare_done = false;
  bool commit_done = false;
  bool blocked = false;

  HT_INFOF("Entering RecoverServerRanges (%p) %s type=%d attempt=%d state=%s",
          (void *)this, m_location.c_str(), m_type, m_attempt,
          OperationState::get_text(state));

  if (m_timeout == 0)
    m_timeout = m_context->props->get_i32("Hypertable.Failover.Timeout");

  // fetch a copy of the recovery plan. if the plan changed: restart
  // the operation
  if (get_new_recovery_plan()) {
    initial_done = false;
    issue_done = false;
    prepare_done = false;
    commit_done = false;
    blocked = false;
    set_state(OperationState::INITIAL);
    m_context->mml_writer->record_state(this);
    state = get_state();
  }

  switch (state) {
  case OperationState::INITIAL:
    // make sure that enough servers are connected
    wait_for_quorum(blocked);
    if (blocked)
      break;

    // if there are no ranges then there is nothing to do
    if (m_ranges.size() == 0) {
      String label_str = label();
      HT_INFO_OUT << label_str << " num_fragments=" << m_fragments.size()
          << ", num_ranges=" << m_ranges.size()
          << " nothing to do, recovery complete" << HT_END;
      complete_ok();
      break;
    }

    set_state(OperationState::PHANTOM_LOAD);
    m_context->mml_writer->record_state(this);
    HT_MAYBE_FAIL(format("recover-server-ranges-%s-1", m_type_str.c_str()));
    initial_done = true;

    // fall through

  case OperationState::PHANTOM_LOAD:
    // make sure that enough servers are connected
    wait_for_quorum(blocked);
    if (blocked)
      break;

    // First ask the destination servers to load the phantom ranges. In 
    // case any request fails go back to INITIAL state and recreate the
    // recovery plan. 
    if (!initial_done && !validate_recovery_plan()) {
      set_state(OperationState::INITIAL);
      m_context->mml_writer->record_state(this);
      HT_MAYBE_FAIL(format("recover-server-ranges-%s-2.1", m_type_str.c_str()));
      break;
    }
    try {
      if (!phantom_load_ranges()) {
        // repeat phantom load
        HT_MAYBE_FAIL(format("recover-server-ranges-%s-3.1",
                    m_type_str.c_str()));
        break;
      }
    }
    catch (Exception &e) {
      HT_ERROR_OUT << e << HT_END;
      HT_THROW(e.code(), e.what());
    }
    set_state(OperationState::REPLAY_FRAGMENTS);
    m_context->mml_writer->record_state(this);
    HT_MAYBE_FAIL(format("recover-server-ranges-%s-4.1", m_type_str.c_str()));
    issue_done = true;

    // fall through to replay fragments

  case OperationState::REPLAY_FRAGMENTS:
    // make sure that enough servers are connected
    wait_for_quorum(blocked);
    if (blocked)
      break;

    // issue play requests to the destination servers. If the
    // request fails, go back to INITIAL state and recreate the recovery plan.
    // If requests succeed, then fall through to WAIT_FOR_COMPLETION state.
    // The only information to persist at the end of this stage is if we
    // failed to connect to a player. That info will be used in the
    // retries state.
    if (!initial_done && !validate_recovery_plan()) {
      set_state(OperationState::INITIAL);
      m_context->mml_writer->record_state(this);
      HT_MAYBE_FAIL(format("recover-server-ranges-%s-2.2", m_type_str.c_str()));
      break;
    }
    try {
      if (!replay_fragments()) {
        // repeat replaying fragments
        m_context->mml_writer->record_state(this);
        HT_MAYBE_FAIL(format("recover-server-ranges-%s-3.2", m_type_str.c_str()));
        break;
      }
    }
    catch (Exception &e) {
      HT_ERROR_OUT << e << HT_END;
      HT_THROW(e.code(), e.what());
    }
    set_state(OperationState::PREPARE);
    m_context->mml_writer->record_state(this);
    HT_MAYBE_FAIL(format("recover-server-ranges-%s-4.2", m_type_str.c_str()));
    issue_done = true;

    // fall through to prepare

  case OperationState::PREPARE:
    // make sure that enough servers are connected
    wait_for_quorum(blocked);
    if (blocked)
      break;

    if (!issue_done && !validate_recovery_plan()) {
      set_state(OperationState::INITIAL);
      m_context->mml_writer->record_state(this);
      HT_MAYBE_FAIL(format("recover-server-ranges-%s-5", m_type_str.c_str()));
      break;
    }
    try {
      // tell destination servers to merge fragment data into range,
      // link in transfer logs to commit log
      if (!prepare_to_commit()) {
        // repeat prepare to commit
        m_context->mml_writer->record_state(this);
        HT_MAYBE_FAIL(format("recover-server-ranges-%s-6", m_type_str.c_str()));
        break;
      }
    }
    catch (Exception &e) {
      HT_ERROR_OUT << e << HT_END;
      HT_THROW(e.code(), e.what());
    }
    set_state(OperationState::COMMIT);
    m_context->mml_writer->record_state(this);
    HT_MAYBE_FAIL(format("recover-server-ranges-%s-7", m_type_str.c_str()));
    prepare_done = true;

    // fall through to commit

  case OperationState::COMMIT:
    // make sure that enough servers are connected
    wait_for_quorum(blocked);
    if (blocked)
      break;

    // Tell destination servers to update metadata and flip ranges live.
    // Persist in rsml and mark range as busy.
    // Finally tell rangeservers to unmark "busy" ranges.
    if (!prepare_done && !validate_recovery_plan()) {
      set_state(OperationState::INITIAL);
      m_context->mml_writer->record_state(this);
      HT_MAYBE_FAIL(format("recover-server-ranges-%s-8", m_type_str.c_str()));
      break;
    }
    if (!commit()) {
      // repeat commit
      m_context->mml_writer->record_state(this);
      HT_MAYBE_FAIL(format("recover-server-ranges-%s-9", m_type_str.c_str()));
      break;
    }
    set_state(OperationState::ACKNOWLEDGE);
    m_context->mml_writer->record_state(this);
    HT_MAYBE_FAIL(format("recover-server-ranges-%s-10", m_type_str.c_str()));
    commit_done = true;

    // fall through

  case OperationState::ACKNOWLEDGE:
    // make sure that enough servers are connected
    wait_for_quorum(blocked);
    if (blocked)
      break;

    if (!commit_done && !validate_recovery_plan()) {
      set_state(OperationState::INITIAL);
      m_context->mml_writer->record_state(this);
      HT_MAYBE_FAIL(format("recover-server-ranges-%s-11", m_type_str.c_str()));
      break;
    }
    if (!acknowledge()) {
      // repeat acknowledge
      m_context->mml_writer->record_state(this);
      HT_MAYBE_FAIL(format("recover-server-ranges-%s-12", m_type_str.c_str()));
      break;
    }
    HT_INFOF("RecoverServerRanges complete for server %s attempt=%d type=%d "
            "state=%s", m_location.c_str(), m_attempt, m_type,
            OperationState::get_text(get_state()));
    complete_ok();
    HT_MAYBE_FAIL(format("recover-server-ranges-%s-13", m_type_str.c_str()));
    break;

  default:
    HT_FATALF("Unrecognized state %d", state);
    break;
  }

  HT_INFOF("Leaving RecoverServerRanges %s attempt=%d type=%d state=%s",
          m_location.c_str(), m_attempt, m_type,
          OperationState::get_text(get_state()));
}

void OperationRecoverRanges::display_state(std::ostream &os) {
  os << " location=" << m_location << " attempt=" << m_attempt << " type="
     << m_type << " num_ranges=" << m_ranges.size() << " num_fragments="
     << m_fragments.size() << " recovery_plan type=" << m_type
     << " state=" << OperationState::get_text(get_state());
}

const String OperationRecoverRanges::name() {
  return "OperationRecoverRanges";
}

const String OperationRecoverRanges::label() {
  return format("RecoverServerRanges %s type=%s",
          m_location.c_str(), m_type_str.c_str());
}

void OperationRecoverRanges::initialize_obstructions_dependencies() {
  ScopedLock lock(m_mutex);
  switch (m_type) {
  case RangeSpec::ROOT:
    m_obstructions.insert(Dependency::ROOT);
    break;
  case RangeSpec::METADATA:
    m_obstructions.insert(Dependency::METADATA);
    m_dependencies.insert(Dependency::ROOT);
    break;
  case RangeSpec::SYSTEM:
    m_obstructions.insert(Dependency::SYSTEM);
    m_dependencies.insert(Dependency::ROOT);
    m_dependencies.insert(Dependency::METADATA);
    break;
  case RangeSpec::USER:
    m_obstructions.insert(format("%s-user", m_location.c_str()));
    m_dependencies.insert(Dependency::ROOT);
    m_dependencies.insert(Dependency::METADATA);
    m_dependencies.insert(Dependency::SYSTEM);
    break;
  }
}

size_t OperationRecoverRanges::encoded_state_length() const {
  size_t len = Serialization::encoded_length_vstr(m_location) + 4 + 4;
  return len;
}

void OperationRecoverRanges::encode_state(uint8_t **bufp) const {
  Serialization::encode_vstr(bufp, m_location);
  Serialization::encode_i32(bufp, m_type);
  Serialization::encode_i32(bufp, m_attempt);
}

void OperationRecoverRanges::decode_state(const uint8_t **bufp,
        size_t *remainp) {
  decode_request(bufp, remainp);
}

void OperationRecoverRanges::decode_request(const uint8_t **bufp,
        size_t *remainp) {
  m_location = Serialization::decode_vstr(bufp, remainp);
  m_type = Serialization::decode_i32(bufp, remainp);
  m_attempt = Serialization::decode_i32(bufp, remainp);
  set_type_str();
  m_timeout = 0;
}

bool OperationRecoverRanges::phantom_load_ranges() {
  // In case of replay failures:
  // master looks at the old plan, reassigns fragments / ranges off the newly
  // failed machines, then replays the whole plan again from start.
  // Destination servers keep track of the state of the replay, if they have
  // already received a complete message from a player then they simply
  // inform the player and the player skips over data to that range.
  // Players are dumb and store (persist) no state other than in memory
  // plan and map of ranges to skip over.
  // State is stored on destination servers (phantom_load state) and the
  // master (plan).
  //
  // Tell destination rangeservers to "phantom-load" the ranges
  RangeServerClient rsc(m_context->comm);
  CommAddress addr;
  bool success = true;
  m_attempt++;
  StringSet locations;
  m_plan.receiver_plan.get_locations(locations);
  vector<uint32_t> fragments;

  m_plan.replay_plan.get_fragments(fragments);
  foreach_ht (const String &location, locations) {
    addr.set_proxy(location);
    vector<QualifiedRangeStateSpec> ranges;
    m_plan.receiver_plan.get_range_state_specs(location.c_str(), ranges);
    try {
      HT_INFO_OUT << "Issue phantom_load for " << ranges.size()
           << " ranges to " << location << HT_END;
      HT_MAYBE_FAIL(format("recover-server-ranges-%s-replay-commit-log", 
                  m_type_str.c_str()));
      rsc.phantom_load(addr, m_location, fragments, ranges);
    }
    catch (Exception &e) {
      success = false;
      HT_ERROR_OUT << e << HT_END;
      break;
    }
  }
  if (!success)
    HT_ERROR_OUT << "Failed to issue phantom_load calls" << HT_END;

  return success;
}

bool OperationRecoverRanges::replay_fragments() {
  // The Master then kicks off players and waits...
  // If players are already in progress (from a previous run) they
  // just return success. When a player completes is calls into some
  // special method (FragmentReplayed) on the master with a recovery id,
  // fragment id, and a list of failed receivers.
  // This special method then stores this info and decrements the var on
  // the condition variable.
  // The synchronization object can be stored in a map in the context
  // object and shared between the OperationRecoverRanges obj and
  // the OperationFragmentReplayed obj.
  //
  // First tell destination rangeservers to "phantom-load" the ranges
  RangeServerClient rsc(m_context->comm);
  CommAddress addr;
  bool success = true;
  m_attempt++;
  StringSet locations;
  m_plan.receiver_plan.get_locations(locations);
  vector<uint32_t> fragments;
  m_plan.replay_plan.get_fragments(fragments);

  // kick off commit log replay and wait for completion
  RecoveryReplayCounterPtr counter = 
      m_context->recovery_state().create_replay_counter(id(), m_attempt);

  StringSet replay_locations;
  m_plan.replay_plan.get_locations(replay_locations);

  foreach_ht(const String &location, replay_locations) {
    bool added = false;
    try {
      fragments.clear();
      m_plan.replay_plan.get_fragments(location.c_str(), fragments);
      addr.set_proxy(location);
      counter->add(fragments.size());
      added = true;
      HT_INFO_OUT << "Issue replay_fragments for " << fragments.size()
          << " fragments to " << location << " (" << m_type_str << ")"
          << HT_END;
      rsc.replay_fragments(addr, id(), m_attempt, m_location, m_type, fragments,
                         m_plan.receiver_plan, m_timeout);
    }
    catch (Exception &e) {
      success = false;
      HT_ERROR_OUT << e << HT_END;
      if (added)
        counter->set_errors(fragments, e.code());
    }
  }

  Timer tt(m_timeout);
  if (!counter->wait_for_completion(tt)) {
    HT_ERROR_OUT << "Commit log replay failed" << HT_END;
    success = false;
  }
  m_context->recovery_state().erase_replay_counter(id());
  // at this point all the players have finished or failed replaying
  // their fragments
  return success;
}

bool OperationRecoverRanges::validate_recovery_plan() {
  HT_ASSERT(m_plan.type != RangeSpec::UNKNOWN);

  return (m_plan_generation
          == m_context->get_balance_plan_authority()->get_generation());
}

void OperationRecoverRanges::wait_for_quorum(bool &blocked) {
  blocked = false;
  StringSet active_locations;
  m_context->get_connected_servers(active_locations);
  size_t total_servers = m_context->server_count();
  size_t quorum = (total_servers * 
          m_context->props->get_i32("Hypertable.Failover.Quorum.Percentage")) 
            / 100;

  if (active_locations.size() < quorum || active_locations.size() == 0) {
    blocked = true;
    // wait for at least half the servers to be up before proceeding
    HT_INFO_OUT << "Only " << active_locations.size()
        << " servers ready, total servers=" << total_servers << " quorum="
        << quorum << ", wait for servers" << HT_END;

    OperationPtr op = new OperationRecoveryBlocker(m_context);
    m_context->op->add_operation(op);
  }
}

bool OperationRecoverRanges::get_new_recovery_plan() {
  if (m_plan_generation ==
          m_context->get_balance_plan_authority()->get_generation()) {
    HT_INFOF("Balance plan generation is still at %d, not fetching a new one",
            m_plan_generation);
    return false;
  }

  m_plan.clear();
  m_context->get_balance_plan_authority()->copy_recovery_plan(m_location,
            m_type, m_plan, m_plan_generation);
  m_plan.receiver_plan.get_range_state_specs(m_ranges);
  m_plan.replay_plan.get_fragments(m_fragments);
  HT_INFOF("Retrieving a new balance plan (generation is %d)",
          m_plan_generation);

  return true;
}

void OperationRecoverRanges::set_type_str() {
  switch (m_type) {
    case RangeSpec::ROOT:
      m_type_str = "root";
      break;
    case RangeSpec::METADATA:
      m_type_str = "metadata";
      break;
    case RangeSpec::SYSTEM:
      m_type_str = "system";
      break;
    case RangeSpec::USER:
      m_type_str = "user";
      break;
    default:
      m_type_str = "UNKNOWN";
  }
}

bool OperationRecoverRanges::prepare_to_commit() {
  StringSet locations;
  RangeServerClient rsc(m_context->comm);
  CommAddress addr;
  bool success = true;

  m_plan.receiver_plan.get_locations(locations);
  RecoveryCounterPtr counter = 
      m_context->recovery_state().create_prepare_counter(id(), m_attempt);

  foreach_ht(const String &location, locations) {
    addr.set_proxy(location);
    vector<QualifiedRangeSpec> ranges;
    m_plan.receiver_plan.get_qualified_range_specs(location.c_str(), ranges);

    HT_INFO_OUT << "Issue phantom_prepare_ranges for " << ranges.size()
        << " ranges to " << location << " (" << m_type_str << ")" << HT_END;
    try {
      counter->add(ranges);
      rsc.phantom_prepare_ranges(addr, id(), m_attempt, m_location,
              ranges, m_timeout);
    }
    catch (Exception &e) {
      success = false;
      counter->set_range_errors(ranges, e.code());
      HT_ERROR_OUT << e << HT_END;
    }
  }
  Timer tt(m_timeout);
  if (!counter->wait_for_completion(tt))
    success = false;
  m_context->recovery_state().erase_prepare_counter(id());
  // at this point all the players have prepared or failed in
  // creating phantom ranges
  return success;
}

bool OperationRecoverRanges::commit() {
  StringSet locations;
  RangeServerClient rsc(m_context->comm);
  CommAddress addr;
  bool success = true;

  m_plan.receiver_plan.get_locations(locations);
  RecoveryCounterPtr counter = 
      m_context->recovery_state().create_commit_counter(id(), m_attempt);

  foreach_ht(const String &location, locations) {
    addr.set_proxy(location);
    vector<QualifiedRangeSpec> ranges;
    m_plan.receiver_plan.get_qualified_range_specs(location.c_str(), ranges);

   try {
      counter->add(ranges);
      HT_INFO_OUT << "Issue phantom_commit_ranges for " << ranges.size()
          << " ranges to " << location << HT_END;
      rsc.phantom_commit_ranges(addr, id(), m_attempt, m_location,
              ranges, m_timeout);
    }
    catch (Exception &e) {
      success = false;
      counter->set_range_errors(ranges, e.code());
      HT_ERROR_OUT << e << HT_END;
    }
  }
  Timer tt(m_timeout);
  if (!counter->wait_for_completion(tt))
    success = false;
  m_context->recovery_state().erase_commit_counter(id());
  // at this point all the players have prepared or failed in creating
  // phantom ranges
  return success;
}

bool OperationRecoverRanges::acknowledge() {
  StringSet locations;
  RangeServerClient rsc(m_context->comm);
  CommAddress addr;
  bool success = true;
  m_plan.receiver_plan.get_locations(locations);

  foreach_ht(const String &location, locations) {
    addr.set_proxy(location);
    vector<QualifiedRangeSpec> ranges;
    vector<QualifiedRangeSpec *> range_ptrs;
    map<QualifiedRangeSpec, int> response_map;
    map<QualifiedRangeSpec, int>::iterator response_map_it;

    m_plan.receiver_plan.get_qualified_range_specs(location.c_str(), ranges);
    foreach_ht(QualifiedRangeSpec &range, ranges)
      range_ptrs.push_back(&range);
    try {
      HT_INFO_OUT << "Issue acknowledge_load for " << range_ptrs.size()
          << " ranges to " << location << HT_END;
      HT_MAYBE_FAIL(format("recover-server-ranges-%s-14", m_type_str.c_str()));
      rsc.acknowledge_load(addr, range_ptrs, response_map);
      response_map_it = response_map.begin();
      while (response_map_it != response_map.end()) {
        if (response_map_it->second != Error::OK)
          HT_WARNF("Problem acknowledging load for %s[%s..%s] - %s",
                  response_map_it->first.table.id,
                  response_map_it->first.range.start_row,
                  response_map_it->first.range.end_row,
                  Error::get_text(response_map_it->second));
        ++response_map_it;
      }
      HT_INFO_OUT << "acknowledge_load complete for " << range_ptrs.size()
          << " ranges to " << location << HT_END;
    }
    catch (Exception &e) {
      success = false;
      HT_ERROR_OUT << e << HT_END;
    }
  }
  // at this point all the players have prepared or failed in
  // creating phantom ranges
  return success;
}

