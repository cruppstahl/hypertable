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
#include "Common/Serialization.h"
#include "Common/Mutex.h"

#include "Hypertable/Lib/CommitLogReader.h"

#include "BalancePlanAuthority.h"
#include "Utility.h"
#include "Context.h"

using namespace Hypertable;

BalancePlanAuthority::BalancePlanAuthority(ContextPtr context,
        MetaLog::WriterPtr &mml_writer)
  : MetaLog::Entity(MetaLog::EntityType::BALANCE_PLAN_AUTHORITY),
    m_context(context), m_mml_writer(mml_writer), m_generation(0)
{
  HT_ASSERT(m_mml_writer);
  m_mml_writer->record_state(this);
}

BalancePlanAuthority::BalancePlanAuthority(ContextPtr context,
        MetaLog::WriterPtr &mml_writer, const MetaLog::EntityHeader &header)
  : MetaLog::Entity(header), m_context(context), m_mml_writer(mml_writer),
    m_generation(0)
{
}

void
BalancePlanAuthority::display(std::ostream &os)
{
  ScopedLock lock(m_mutex);
  os << " generation " << m_generation << " ";
  RecoveryPlanMap::iterator it = m_map.begin();
  while (it != m_map.end()) {
    os << it->first << ": ";
    for (int i = 0; i < 4; i++)
      if (it->second.plans[i])
        os << *(it->second.plans[i]) << " ";
    it++;
  }
}

size_t 
BalancePlanAuthority::encoded_length() const
{
  size_t len = 4 + 4;
  RecoveryPlanMap::const_iterator it = m_map.begin();
  while (it != m_map.end()) {
    len += Serialization::encoded_length_vstr(it->first);
    for (int i = 0; i < 4; i++) {
      len += 1;
      if (it->second.plans[i])
        len += it->second.plans[i]->encoded_length();
    }
    it++;
  }
  return len;
}

void 
BalancePlanAuthority::encode(uint8_t **bufp) const
{
  Serialization::encode_i32(bufp, m_generation);
  Serialization::encode_i32(bufp, m_map.size());
  RecoveryPlanMap::const_iterator it = m_map.begin();
  while (it != m_map.end()) {
    Serialization::encode_vstr(bufp, it->first);
    for (int i = 0; i < 4; i++) {
      if (it->second.plans[i]) {
        Serialization::encode_bool(bufp, true);
        it->second.plans[i]->encode(bufp);
      }
      else
        Serialization::encode_bool(bufp, false);
    }
    it++;
  }
}

void 
BalancePlanAuthority::decode(const uint8_t **bufp, size_t *remainp)
{
  ScopedLock lock(m_mutex);
  m_generation = Serialization::decode_i32(bufp, remainp);

  int num_servers = Serialization::decode_i32(bufp, remainp);
  for (int i = 0; i < num_servers; i++) {
    String rs = Serialization::decode_vstr(bufp, remainp);
    RecoveryPlans plans;
    for (int j = 0; j < 4; j++) {
      bool b = Serialization::decode_bool(bufp, remainp);
      if (!b) {
        plans.plans[j] = 0;
        continue;
      }
      plans.plans[j] = new RangeRecoveryPlan();
      plans.plans[j]->decode(bufp, remainp);
    }
    m_map[rs] = plans;
  }
}

void
BalancePlanAuthority::set_mml_writer(MetaLog::WriterPtr &mml_writer)
{
  ScopedLock lock(m_mutex);
  m_mml_writer = mml_writer;
}

void
BalancePlanAuthority::copy_recovery_plan(const String &location, int type,
        RangeRecoveryPlan &out, int &generation)
{
  ScopedLock lock(m_mutex);
  HT_ASSERT(m_map.find(location) != m_map.end());
  RangeRecoveryPlanPtr plan = m_map[location].plans[type];
  HT_ASSERT(plan->type == type);

  out.type = type;
  out.replay_plan = plan->replay_plan;
  plan->receiver_plan.copy(out.receiver_plan);
  generation = m_generation;
}

void
BalancePlanAuthority::remove_recovery_plan(const String &location)
{
  ScopedLock lock(m_mutex);
  RecoveryPlanMap::iterator it = m_map.find(location);
  if (it == m_map.end())
     return;
  m_map.erase(it);
  m_mml_writer->record_state(this);
}

void
BalancePlanAuthority::create_recovery_plan(const String &location,
        const vector<QualifiedRangeStateSpecManaged> &root_ranges, 
        const vector<QualifiedRangeStateSpecManaged> &metadata_ranges,
        const vector<QualifiedRangeStateSpecManaged> &system_ranges, 
        const vector<QualifiedRangeStateSpecManaged> &user_ranges)
{
  ScopedLock lock(m_mutex);

  HT_INFO_OUT << "Creating recovery plan for " << location << HT_END;

  // check if this RangeServer was already recovered; if yes then do not add
  // a new plan (this happens i.e. in rangeserver-failover-master-16)
  if (m_map.find(location) != m_map.end())
    return;

  // walk through the existing plans and move all ranges from that server
  // to other servers
  RecoveryPlanMap::iterator it;
  for (it = m_map.begin(); it != m_map.end(); ++it) {
    if (it->second.plans[RangeSpec::ROOT])
      update_range_plan(it->second.plans[RangeSpec::ROOT], location);
    if (it->second.plans[RangeSpec::METADATA])
      update_range_plan(it->second.plans[RangeSpec::METADATA], location);
    if (it->second.plans[RangeSpec::SYSTEM])
      update_range_plan(it->second.plans[RangeSpec::SYSTEM], location);
    if (it->second.plans[RangeSpec::USER])
      update_range_plan(it->second.plans[RangeSpec::USER], location);
  }

  // then create the new plan for the failed server
  RecoveryPlans plans;
  plans.plans[RangeSpec::ROOT]
      = create_range_plan(location, RangeSpec::ROOT, root_ranges);
  plans.plans[RangeSpec::METADATA]
      = create_range_plan(location, RangeSpec::METADATA, metadata_ranges);
  plans.plans[RangeSpec::SYSTEM]
      = create_range_plan(location, RangeSpec::SYSTEM, system_ranges);
  plans.plans[RangeSpec::USER]
      = create_range_plan(location, RangeSpec::USER, user_ranges);

  // Remove ALL ranges that are FROM the failing node or are about
  // to be moved TO the failing node. These ranges are already part
  // of the balance plan anyway.
  // TODO really? verify this
  MoveSetT::iterator iter = m_current_set.begin();
  while (iter != m_current_set.end()) {
    if (location == (*iter)->dest_location) {
      HT_INFO_OUT << "XXX " << location << ": " << *iter << HT_END;
      MoveSetT::iterator iter2 = iter;
      m_current_set.erase(iter2);
    }
    ++iter;
  }

  // store the new plan
  m_generation++;
  m_map[location] = plans;
  m_mml_writer->record_state(this);

  lock.unlock(); // otherwise operator<< will deadlock
  HT_INFO_OUT << "Global recovery plan was modified: " << *this << HT_END;
}

RangeRecoveryPlan * 
BalancePlanAuthority::create_range_plan(const String &location, int type,
        const vector<QualifiedRangeStateSpecManaged> &ranges)
{
  if (ranges.empty())
    return 0;

  const char *type_strings[] = { "root", "metadata", "system", "user", "UNKNOWN" };

  StringSet active_locations;
  m_context->get_connected_servers(active_locations);

  vector<uint32_t> fragments;

  RangeRecoveryPlan *plan = new RangeRecoveryPlan();
  plan->type = type;

  // read the fragments from the commit log
  String log_dir = m_context->toplevel_dir + "/servers/" + location
      + "/log/" + type_strings[type];
  CommitLogReader clr(m_context->dfs, log_dir);
  clr.get_init_fragment_ids(fragments);

  StringSet::const_iterator location_it = active_locations.begin();

  // round robin through the locations and assign the ranges
  foreach (const QualifiedRangeStateSpec &range, ranges) {
    if (location_it == active_locations.end())
      location_it = active_locations.begin();
    plan->receiver_plan.insert(location_it->c_str(),
            range.qualified_range.table,
            range.qualified_range.range, range.state);
    ++location_it;
  }

  location_it = active_locations.begin();
  // round robin through the locations and assign the fragments
  foreach (uint32_t fragment, fragments) {
    if (location_it == active_locations.end())
      location_it = active_locations.begin();
    plan->replay_plan.insert(location_it->c_str(), fragment);
    ++location_it;
  }

  return plan;
}

void
BalancePlanAuthority::update_range_plan(RangeRecoveryPlanPtr &plan,
        const String &location)
{
  StringSet active_locations;
  m_context->get_connected_servers(active_locations);

  HT_ASSERT(active_locations.find(location) == active_locations.end());

  StringSet::const_iterator location_it = active_locations.begin();

  vector<uint32_t> fragments;
  plan->replay_plan.get_fragments(location.c_str(), fragments);
  // round robin through the locations and assign the fragments
  foreach (uint32_t fragment, fragments) {
    if (location_it == active_locations.end())
      location_it = active_locations.begin();
    plan->replay_plan.insert(location_it->c_str(), fragment);
    ++location_it;
  }

  location_it = active_locations.begin();
  vector<QualifiedRangeStateSpec> ranges;
  plan->receiver_plan.get_range_state_specs(location.c_str(), ranges);
  // round robin through the locations and assign the ranges
  foreach (const QualifiedRangeStateSpec &range, ranges) {
    if (location_it == active_locations.end())
      location_it = active_locations.begin();
    plan->receiver_plan.insert(location_it->c_str(),
            range.qualified_range.table,
            range.qualified_range.range, range.state);
    ++location_it;
  }
}

void
BalancePlanAuthority::register_balance_plan(BalancePlanPtr &plan) {
  ScopedLock lock(m_mutex);

  // Insert moves into current set
  foreach (RangeMoveSpecPtr &move, plan->moves)
    m_current_set.insert(move);

  HT_INFO_OUT << "Balance plan registered move " << plan->moves.size()
        << " ranges" << ", BalancePlan = " << *plan<< HT_END;
}

bool
BalancePlanAuthority::get_balance_destination(const TableIdentifier &table,
                const RangeSpec &range, String &location) {
  ScopedLock lock(m_mutex);

  RangeMoveSpecPtr move_spec = new RangeMoveSpec();

  move_spec->table = table;
  move_spec->table.generation = 0;
  move_spec->range = range;

  MoveSetT::iterator iter;

  if ((iter = m_current_set.find(move_spec)) != m_current_set.end())
    location = (*iter)->dest_location;
  else {
    if (!Utility::next_available_server(m_context, location))
      return false;
  }

  return true;
}

bool
BalancePlanAuthority::balance_move_complete(const TableIdentifier &table,
                const RangeSpec &range, int32_t error) {
  ScopedLock lock(m_mutex);
  RangeMoveSpecPtr move_spec = new RangeMoveSpec();

  move_spec->table = table;
  move_spec->table.generation = 0;
  move_spec->range = range;

  MoveSetT::iterator iter;

  if ((iter = m_current_set.find(move_spec)) != m_current_set.end()) {
    // the 'complete' and 'error' fields currently are not in use
    (*iter)->complete = true;
    (*iter)->error = error;
    m_current_set.erase(iter);
  }

  return true;
}

