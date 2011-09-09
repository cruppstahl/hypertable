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

#ifndef HYPERTABLE_LOADBALANCER_H
#define HYPERTABLE_LOADBALANCER_H

#include <set>

#include <boost/thread/condition.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "Common/Mutex.h"
#include "Common/ReferenceCount.h"

#include "Hypertable/Lib/BalancePlan.h"

#include "RSMetrics.h"
#include "Context.h"

namespace Hypertable {
  using namespace boost::posix_time;

  class OperationBalance;

  class LoadBalancer : public ReferenceCount {
  public:
    LoadBalancer(ContextPtr context);

    // starts a balance operation
    void balance(const String &algorithm = String());

    // transfers monitoring statistics from the RangeServers; invoked by
    // OperationGatherStatistics
    void transfer_monitoring_data(vector<RangeServerStatistics> &stats);

  private:
    void calculate_balance_plan(const String &algorithm,
                    BalancePlanPtr &plan);
    void distribute_load(const boost::posix_time::ptime &now,
                    vector<RangeServerStatistics> &range_server_stats,
                    BalancePlanPtr &plan);
    void distribute_table_ranges(vector<RangeServerStatistics> &stats,
                    BalancePlanPtr &plan);
    void offload_servers(vector<RangeServerStatistics> &stats,
                    set<String> &offload, BalancePlanPtr &plan);

    void get_unbalanced_servers(const std::vector<RangeServerStatistics> &stats);

    Mutex m_mutex;
    Mutex m_data_mutex;
    ContextPtr m_context;
    uint32_t m_balance_interval;
    uint32_t m_balance_wait;
    time_duration m_balance_window_start;
    time_duration m_balance_window_end;
    ptime m_last_balance_time;
    double m_balance_loadavg_threshold;
    std::vector <RangeServerConnectionPtr> m_unbalanced_servers;
    bool m_enabled;
    bool m_waiting_for_servers;
    std::vector <RangeServerStatistics> m_range_server_stats;
    ptime m_wait_time_start;
    BalancePlanPtr m_plan;
  };

  typedef intrusive_ptr<LoadBalancer> LoadBalancerPtr;

} // namespace Hypertable

#endif // HYPERTABLE_LOADBALANCER_H
