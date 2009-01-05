/** -*- c++ -*-
 * Copyright (C) 2008 Doug Judd (Zvents, Inc.)
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2 of the
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
#include <cassert>

extern "C" {
#include <math.h>
#include <sys/time.h>
#include <sys/resource.h>
}

#include "Common/FileUtils.h"
#include "Common/md5.h"
#include "Common/StringExt.h"
#include "Common/SystemInfo.h"

#include "Hypertable/Lib/CommitLog.h"
#include "Hypertable/Lib/Defaults.h"
#include "Hypertable/Lib/Key.h"
#include "Hypertable/Lib/Stat.h"
#include "Hypertable/Lib/RangeServerMetaLogReader.h"
#include "Hypertable/Lib/RangeServerMetaLogEntries.h"
#include "Hypertable/Lib/RangeServerProtocol.h"

#include "DfsBroker/Lib/Client.h"

#include "FillScanBlock.h"
#include "Global.h"
#include "HandlerFactory.h"
#include "MaintenanceQueue.h"
#include "RangeServer.h"
#include "ScanContext.h"
#include "MaintenanceTaskCompaction.h"
#include "MaintenanceTaskLogCleanup.h"
#include "MaintenanceTaskSplit.h"

using namespace std;
using namespace Hypertable;
using namespace Serialization;


RangeServer::RangeServer(PropertiesPtr &props, ConnectionManagerPtr &conn_mgr,
    ApplicationQueuePtr &app_queue, Hyperspace::SessionPtr &hyperspace)
  : m_root_replay_finished(false), m_metadata_replay_finished(false),
    m_replay_finished(false), m_props(props), m_verbose(false),
    m_conn_manager(conn_mgr), m_app_queue(app_queue), m_hyperspace(hyperspace),
    m_last_commit_log_clean(0), m_bytes_loaded(0) {

  uint16_t port;
  uint32_t maintenance_threads = 1;
  Comm *comm = conn_mgr->get_comm();
  SubProperties cfg(props, "Hypertable.RangeServer.");

  m_verbose = props->get_bool("verbose");
  Global::range_max_bytes = cfg.get_i64("Range.MaxBytes");
  Global::access_group_max_files = cfg.get_i32("AccessGroup.MaxFiles");
  Global::access_group_merge_files = cfg.get_i32("AccessGroup.MergeFiles");
  Global::access_group_max_mem = cfg.get_i64("AccessGroup.MaxMemory");
  maintenance_threads = cfg.get_i32("MaintenanceThreads");
  port = cfg.get_i16("Port");
  m_scanner_ttl = (time_t)cfg.get_i32("Scanner.Ttl");
  m_timer_interval = cfg.get_i32("Timer.Interval");

  if (m_timer_interval < 1000)
    HT_THROWF(Error::CONFIG_BAD_VALUE, "Hypertable.RangeServer.Timer.Interval "
        "too small: %d", (int)m_timer_interval);

  if (m_scanner_ttl < (time_t)10000) {
    HT_WARNF("Value %u for Hypertable.RangeServer.Scanner.ttl is too small, "
             "setting to 10000", (unsigned int)m_scanner_ttl);
    m_scanner_ttl = (time_t)10000;
  }

  m_max_clock_skew = cfg.get_i32("ClockSkew.Max");

  uint64_t block_cacheMemory = cfg.get_i64("BlockCache.MaxMemory");
  Global::block_cache = new FileBlockCache(block_cacheMemory);

  Global::protocol = new Hypertable::RangeServerProtocol();

  DfsBroker::Client *dfsclient = new DfsBroker::Client(conn_mgr, props);
  int timeout = props->get_i32("DfsBroker.Timeout");

  if (!dfsclient->wait_for_connection(timeout))
    HT_THROW(Error::REQUEST_TIMEOUT, "connecting to DFS Broker");

  Global::dfs = dfsclient;

  m_log_roll_limit = cfg.get_i64("CommitLog.RollLimit");

  /**
   * Check for and connect to commit log DFS broker
   */
  if (cfg.has("CommitLog.DfsBroker.Host")) {
    String loghost = cfg.get_str("CommitLog.DfsBroker.Host");
    uint16_t logport = cfg.get_i16("CommitLog.DfsBroker.Port");
    InetAddr addr(loghost, logport);

    dfsclient = new DfsBroker::Client(conn_mgr, addr, timeout);

    if (!dfsclient->wait_for_connection(30000))
      HT_THROW(Error::REQUEST_TIMEOUT, "connecting to commit log DFS broker");

    Global::log_dfs = dfsclient;
  }
  else
    Global::log_dfs = Global::dfs;

  /**
   * Initialize range server location
   */
  InetAddr addr(System::net_info().primary_addr, port);

  Global::location = addr.format('_');

  // Create the maintenance queue
  Global::maintenance_queue = new MaintenanceQueue(maintenance_threads);

  // Create table info maps
  m_live_map = new TableInfoMap();
  m_replay_map = new TableInfoMap();

  initialize(props);

  /**
   * Listen for incoming connections
   */
  ConnectionHandlerFactoryPtr chfp =
      new HandlerFactory(comm, m_app_queue, this);

  InetAddr listen_addr(INADDR_ANY, port);
  comm->listen(listen_addr, chfp);

  /**
   * Create Master client
   */
  timeout = props->get_i32("Hypertable.Master.Timeout");
  m_master_client = new MasterClient(m_conn_manager, m_hyperspace,
                                     timeout, m_app_queue);
  m_master_connection_handler = new ConnectionHandler(comm, m_app_queue,
                                    this, m_master_client);
  m_master_client->initiate_connection(m_master_connection_handler);

  // Halt maintenance queue processing during recovery
  Global::maintenance_queue->stop();

  local_recover();

  Global::maintenance_queue->start();

  Global::log_prune_threshold_min = cfg.get_i64("CommitLog.PruneThreshold.Min",
      2 * Global::user_log->get_max_fragment_size());
  Global::log_prune_threshold_max = cfg.get_i64("CommitLog.PruneThreshold.Max",
      10 * Global::log_prune_threshold_min);
}


RangeServer::~RangeServer() {
  delete Global::block_cache;
  delete Global::protocol;
  m_hyperspace = 0;
  delete Global::dfs;
  if (Global::log_dfs != Global::dfs)
    delete Global::log_dfs;
  Global::metadata_table_ptr = 0;
  m_master_client = 0;
  m_conn_manager = 0;
  m_app_queue = 0;
}


/**
 * - Figure out and create range server directory
 * - Clear any Range server state (including any partially created commit logs)
 * - Open the commit log
 */
void RangeServer::initialize(PropertiesPtr &props) {

  if (!m_hyperspace->exists("/hypertable/servers")) {
    if (!m_hyperspace->exists("/hypertable"))
      m_hyperspace->mkdir("/hypertable");
    m_hyperspace->mkdir("/hypertable/servers");
  }

  String top_dir("/hypertable/servers/");
  top_dir += Global::location;

  /**
   * Create "server existence" file in Hyperspace and lock it exclusively
   */
  uint32_t lock_status;
  uint32_t oflags = OPEN_FLAG_READ | OPEN_FLAG_WRITE | OPEN_FLAG_CREATE
      | OPEN_FLAG_CREATE | OPEN_FLAG_LOCK;
  HandleCallbackPtr null_callback;

  m_existence_file_handle = m_hyperspace->open(top_dir.c_str(), oflags,
                                               null_callback);

  while (true) {
    lock_status = 0;

    m_hyperspace->try_lock(m_existence_file_handle, LOCK_MODE_EXCLUSIVE,
                           &lock_status, &m_existence_file_sequencer);

    if (lock_status == LOCK_STATUS_GRANTED)
      break;

    HT_INFO_OUT << "Waiting for exclusive lock on hyperspace:/" << top_dir
                << " ..." << HT_END;
    poll(0, 0, 5000);
  }

  Global::log_dir = top_dir + "/log";

  /**
   * Create log directories
   */
  String path;
  try {
    path = Global::log_dir + "/user";
    Global::log_dfs->mkdirs(path);
    path = Global::log_dir + "/range_txn";
    Global::log_dfs->mkdirs(path);
  }
  catch (Exception &e) {
    HT_THROW2F(e.code(), e, "Problem creating commit log directory '%s': %s",
               path.c_str(), e.what());
  }

  HT_INFO_OUT << "log_dir=" << Global::log_dir << HT_END;
}


void RangeServer::local_recover() {
  String meta_log_fname = Global::log_dir + "/range_txn/0.log";
  RangeServerMetaLogReaderPtr rsml_reader;
  CommitLogReaderPtr root_log_reader_ptr;
  CommitLogReaderPtr metadata_log_reader_ptr;
  CommitLogReaderPtr user_log_reader_ptr;
  std::vector<RangePtr> rangev;

  try {
    /**
     * Check for existence of
     * /hypertable/servers/X.X.X.X_port/log/range_txn/0.log file
     */
    if (Global::log_dfs->exists(meta_log_fname)) {
      HT_DEBUG_OUT <<"Found "<< meta_log_fname <<", start recovering"<< HT_END;

      // Load range states
      rsml_reader = new RangeServerMetaLogReader(Global::log_dfs,
                                                 meta_log_fname);
      const RangeStates &range_states = rsml_reader->load_range_states();

      /**
       * First ROOT metadata range
       */
      m_replay_group = RangeServerProtocol::GROUP_METADATA_ROOT;

      // clear the replay map
      m_replay_map->clear();

      foreach(const RangeStateInfo *i, range_states) {
        if (i->table.id == 0 && i->range.end_row
            && !strcmp(i->range.end_row, Key::END_ROOT_ROW)) {
          HT_ASSERT(i->transactions.empty());
          replay_load_range(0, &i->table, &i->range, &i->range_state);
        }
      }

      if (!m_replay_map->empty()) {
        root_log_reader_ptr = new CommitLogReader(Global::log_dfs,
                                                  Global::log_dir + "/root");
        replay_log(root_log_reader_ptr);

        // Perform any range specific post-replay tasks
        rangev.clear();
        m_replay_map->get_range_vector(rangev);
        foreach(RangePtr &range_ptr, rangev)
          range_ptr->recovery_finalize();

        m_live_map->merge(m_replay_map);
      }

      // Create root log and wake up anybody waiting for root replay to complete
      {
        ScopedLock lock(m_mutex);
        if (root_log_reader_ptr)
          Global::root_log = new CommitLog(Global::log_dfs, Global::log_dir
              + "/root", m_props, root_log_reader_ptr.get());
        m_root_replay_finished = true;
        m_root_replay_finished_cond.notify_all();
      }

      /**
       * Then recover other metadata ranges
       */
      m_replay_group = RangeServerProtocol::GROUP_METADATA;

      // clear the replay map
      m_replay_map->clear();

      foreach(const RangeStateInfo *i, range_states) {
        if (i->table.id == 0 && !(i->range.end_row
            && !strcmp(i->range.end_row, Key::END_ROOT_ROW)))
          replay_load_range(0, &i->table, &i->range, &i->range_state);
      }

      if (!m_replay_map->empty()) {
        metadata_log_reader_ptr =
            new CommitLogReader(Global::log_dfs, Global::log_dir + "/metadata");
        replay_log(metadata_log_reader_ptr);

        // Perform any range specific post-replay tasks
        rangev.clear();
        m_replay_map->get_range_vector(rangev);
        foreach(RangePtr &range_ptr, rangev)
          range_ptr->recovery_finalize();

        m_live_map->merge(m_replay_map);
      }

      // Create metadata log and wake up anybody waiting for metadata replay to
      // complete
      {
        ScopedLock lock(m_mutex);
        if (metadata_log_reader_ptr)
          Global::metadata_log = new CommitLog(Global::log_dfs,
              Global::log_dir + "/metadata", m_props,
              metadata_log_reader_ptr.get());
        m_metadata_replay_finished = true;
        m_metadata_replay_finished_cond.notify_all();
      }

      /**
       * Then recover the normal ranges
       */
      m_replay_group = RangeServerProtocol::GROUP_USER;

      // clear the replay map
      m_replay_map->clear();

      foreach(const RangeStateInfo *i, range_states) {
        if (i->table.id != 0)
          replay_load_range(0, &i->table, &i->range, &i->range_state);
      }

      if (!m_replay_map->empty()) {
        user_log_reader_ptr = new CommitLogReader(Global::log_dfs,
                                                  Global::log_dir + "/user");
        replay_log(user_log_reader_ptr);

        // Perform any range specific post-replay tasks
        rangev.clear();
        m_replay_map->get_range_vector(rangev);
        foreach(RangePtr &range_ptr, rangev)
          range_ptr->recovery_finalize();

        m_live_map->merge(m_replay_map);
      }


      // Create user log and range txn log and
      // wake up anybody waiting for replay to complete
      {
        ScopedLock lock(m_mutex);
        Global::user_log = new CommitLog(Global::log_dfs, Global::log_dir
            + "/user", m_props, user_log_reader_ptr.get());
        Global::range_log = new RangeServerMetaLog(Global::log_dfs,
                                                   meta_log_fname);
        m_replay_finished = true;
        m_replay_finished_cond.notify_all();
      }

    }
    else {
      ScopedLock lock(m_mutex);

      /**
       *  Create the logs
       */

      if (root_log_reader_ptr)
        Global::root_log = new CommitLog(Global::log_dfs, Global::log_dir
            + "/root", m_props, root_log_reader_ptr.get());

      if (metadata_log_reader_ptr)
        Global::metadata_log = new CommitLog(Global::log_dfs, Global::log_dir
            + "/metadata", m_props, metadata_log_reader_ptr.get());

      Global::user_log = new CommitLog(Global::log_dfs, Global::log_dir
          + "/user", m_props, user_log_reader_ptr.get());

      Global::range_log = new RangeServerMetaLog(Global::log_dfs,
                                                 meta_log_fname);

      m_root_replay_finished = true;
      m_metadata_replay_finished = true;
      m_replay_finished = true;

      m_root_replay_finished_cond.notify_all();
      m_metadata_replay_finished_cond.notify_all();
      m_replay_finished_cond.notify_all();
    }

  }
  catch (Exception &e) {
    HT_ERROR_OUT << e << HT_END;
    HT_ABORT;
  }
}


void RangeServer::replay_log(CommitLogReaderPtr &log_reader_ptr) {
  BlockCompressionHeaderCommitLog header;
  uint8_t *base;
  size_t len;
  TableIdentifier table_id;
  DynamicBuffer dbuf;
  const uint8_t *ptr, *end;
  int64_t revision;
  TableInfoPtr table_info;
  RangePtr range_ptr;
  SerializedKey key;
  ByteString value;
  uint32_t block_count = 0;

  while (log_reader_ptr->next((const uint8_t **)&base, &len, &header)) {

    revision = header.get_revision();

    ptr = base;
    end = base + len;

    table_id.decode(&ptr, &len);

    // Fetch table info
    if (!m_replay_map->get(table_id.id, table_info))
      continue;

    dbuf.ensure(table_id.encoded_length() + 12 + len);
    dbuf.clear();

    dbuf.ptr += 4;  // skip size
    encode_i64(&dbuf.ptr, revision);
    table_id.encode(&dbuf.ptr);
    base = dbuf.ptr;

    while (ptr < end) {

      // extract the key
      key.ptr = ptr;
      ptr += key.length();
      if (ptr > end)
        HT_THROW(Error::REQUEST_TRUNCATED, "Problem decoding key");

      // extract the value
      value.ptr = ptr;
      ptr += value.length();
      if (ptr > end)
        HT_THROW(Error::REQUEST_TRUNCATED, "Problem decoding value");

      // Look for containing range, add to stop mods if not found
      if (!table_info->find_containing_range(key.row(), range_ptr))
        continue;

      // add key/value pair to buffer
      memcpy(dbuf.ptr, key.ptr, ptr-key.ptr);
      dbuf.ptr += ptr-key.ptr;

    }

    uint32_t block_size = dbuf.ptr - base;
    base = dbuf.base;
    encode_i32(&base, block_size);

    replay_update(0, dbuf.base, dbuf.fill());
    block_count++;
  }

  HT_INFOF("Replayed %u blocks of updates from '%s'", block_count,
           log_reader_ptr->get_log_dir().c_str());
}


void
RangeServer::compact(ResponseCallback *cb, const TableIdentifier *table,
                     const RangeSpec *range, uint8_t compaction_type) {
  int error = Error::OK;
  String errmsg;
  TableInfoPtr table_info;
  RangePtr range_ptr;
  bool major = false;

  // Check for major compaction
  if (compaction_type == 1)
    major = true;

  HT_DEBUG_OUT <<"compacting\n"<< *table << *range
               <<"Compaction type="<< (major ? "major" : "minor") << HT_END;

  if (!m_replay_finished)
    wait_for_recovery_finish();

  /**
   * Fetch table info
   */
  if (!m_live_map->get(table->id, table_info)) {
    error = Error::RANGESERVER_RANGE_NOT_FOUND;
    errmsg = "No ranges loaded for table '" + (String)table->name + "'";
    goto abort;
  }

  /**
   * Fetch range info
   */
  if (!table_info->get_range(range, range_ptr)) {
    error = Error::RANGESERVER_RANGE_NOT_FOUND;
    errmsg = format("%s[%s..%s]", table->name,range->start_row,range->end_row);
    goto abort;
  }

  // schedule the compaction
  if (!range_ptr->test_and_set_maintenance())
    Global::maintenance_queue->add(new MaintenanceTaskCompaction(range_ptr,
                                                                 major));

  if ((error = cb->response_ok()) != Error::OK) {
    HT_ERRORF("Problem sending OK response - %s", Error::get_text(error));
  }

  HT_DEBUGF("Compaction (%s) scheduled for table '%s' end row '%s'",
            (major ? "major" : "minor"), table->name, range->end_row);

  error = Error::OK;

 abort:
  if (error != Error::OK) {
    HT_ERRORF("%s '%s'", Error::get_text(error), errmsg.c_str());
    if ((error = cb->error(error, errmsg)) != Error::OK) {
      HT_ERRORF("Problem sending error response - %s", Error::get_text(error));
    }
  }
}


void
RangeServer::create_scanner(ResponseCallbackCreateScanner *cb,
    const TableIdentifier *table, const RangeSpec *range,
    const ScanSpec *scan_spec) {
  int error = Error::OK;
  String errmsg;
  TableInfoPtr table_info;
  RangePtr range_ptr;
  CellListScannerPtr scanner_ptr;
  bool more = true;
  uint32_t id;
  SchemaPtr schema;
  ScanContextPtr scan_ctx;
  bool decrement_needed=false;

  HT_DEBUG_OUT <<"Creating scanner:\n"<< *table << *range
               << *scan_spec << HT_END;

  if (!m_replay_finished)
    wait_for_recovery_finish(table, range);

  try {
    DynamicBuffer rbuf;

    if (scan_spec->row_intervals.size() > 0) {
      if (scan_spec->row_intervals.size() > 1)
        HT_THROW(Error::RANGESERVER_BAD_SCAN_SPEC,
                 "can only scan one row interval");
      if (scan_spec->cell_intervals.size() > 0)
        HT_THROW(Error::RANGESERVER_BAD_SCAN_SPEC,
                 "both row and cell intervals defined");
    }

    if (scan_spec->cell_intervals.size() > 1)
      HT_THROW(Error::RANGESERVER_BAD_SCAN_SPEC,
               "can only scan one cell interval");

    if (!m_live_map->get(table->id, table_info))
      HT_THROWF(Error::RANGESERVER_RANGE_NOT_FOUND, "unknown table '%s'",
                table->name);

    if (!table_info->get_range(range, range_ptr))
      HT_THROWF(Error::RANGESERVER_RANGE_NOT_FOUND, "(a) %s[%s..%s]",
                table->name, range->start_row, range->end_row);

    schema = table_info->get_schema();

    range_ptr->increment_scan_counter();
    decrement_needed = true;

    // Check to see if range jus shrunk
    if (strcmp(range_ptr->start_row().c_str(), range->start_row) ||
        strcmp(range_ptr->end_row().c_str(), range->end_row))
      HT_THROWF(Error::RANGESERVER_RANGE_NOT_FOUND, "(b) %s[%s..%s]",
                table->name, range->start_row, range->end_row);

    scan_ctx = new ScanContext(range_ptr->get_scan_revision(),
                               scan_spec, range, schema);

    scanner_ptr = range_ptr->create_scanner(scan_ctx);

    range_ptr->decrement_scan_counter();
    decrement_needed = false;

    size_t count;
    more = FillScanBlock(scanner_ptr, rbuf, &count);

    id = (more) ? Global::scanner_map.put(scanner_ptr, range_ptr) : 0;

    HT_DEBUGF("Successfully created scanner (id=%u) on table '%s', returning "
              "%d k/v pairs", id, table->name, (int)count);

    /**
     *  Send back data
     */
    {
      short moreflag = more ? 0 : 1;
      StaticBuffer ext(rbuf);
      if ((error = cb->response(moreflag, id, ext)) != Error::OK) {
        HT_ERRORF("Problem sending OK response - %s", Error::get_text(error));
      }
    }
  }
  catch (Hypertable::Exception &e) {
    int error;
    if (decrement_needed)
      range_ptr->decrement_scan_counter();
    if (e.code() == Error::RANGESERVER_RANGE_NOT_FOUND)
      HT_INFO_OUT << e << HT_END;
    else
      HT_ERROR_OUT << e << HT_END;
    if ((error = cb->error(e.code(), e.what())) != Error::OK) {
      HT_ERRORF("Problem sending error response - %s", Error::get_text(error));
    }
  }
}


void RangeServer::destroy_scanner(ResponseCallback *cb, uint32_t scanner_id) {
  HT_DEBUGF("destroying scanner id=%u", scanner_id);
  Global::scanner_map.remove(scanner_id);
  cb->response_ok();
}


void
RangeServer::fetch_scanblock(ResponseCallbackFetchScanblock *cb,
                             uint32_t scanner_id) {
  String errmsg;
  int error = Error::OK;
  CellListScannerPtr scanner_ptr;
  RangePtr range_ptr;
  bool more = true;
  DynamicBuffer rbuf;

  HT_DEBUG_OUT <<"Scanner ID = " << scanner_id << HT_END;

  if (!Global::scanner_map.get(scanner_id, scanner_ptr, range_ptr)) {
    error = Error::RANGESERVER_INVALID_SCANNER_ID;
    char tbuf[32];
    sprintf(tbuf, "%d", scanner_id);
    errmsg = tbuf;
    goto abort;
  }

  size_t count;
  more = FillScanBlock(scanner_ptr, rbuf, &count);

  if (!more)
    Global::scanner_map.remove(scanner_id);

  /**
   *  Send back data
   */
  {
    short moreflag = more ? 0 : 1;
    StaticBuffer ext(rbuf);

    if ((error = cb->response(moreflag, scanner_id, ext)) != Error::OK) {
      HT_ERRORF("Problem sending OK response - %s", Error::get_text(error));
    }

    HT_DEBUGF("Successfully fetched %u bytes (%d k/v pairs) of scan data",
              ext.size-4, (int)count);
  }

  error = Error::OK;

 abort:
  if (error != Error::OK) {
    HT_ERRORF("%s '%s'", Error::get_text(error), errmsg.c_str());
    if ((error = cb->error(error, errmsg)) != Error::OK) {
      HT_ERRORF("Problem sending error response - %s", Error::get_text(error));
    }
  }
}


void
RangeServer::load_range(ResponseCallback *cb, const TableIdentifier *table,
    const RangeSpec *range, const char *transfer_log_dir,
    const RangeState *range_state) {
  String errmsg;
  int error = Error::OK;
  SchemaPtr schema;
  TableInfoPtr table_info;
  RangePtr range_ptr;
  String table_dfsdir;
  String range_dfsdir;
  char md5DigestStr[33];
  bool register_table = false;
  bool is_root = table->id == 0 && (*range->start_row == 0)
      && !strcmp(range->end_row, Key::END_ROOT_ROW);
  TableScannerPtr scanner_ptr;
  TableMutatorPtr mutator_ptr;
  KeySpec key;
  String metadata_key_str;

  HT_DEBUG_OUT <<"Loading range: "<< *table <<" "<< *range << HT_END;

  if (!m_replay_finished)
    wait_for_recovery_finish();

  try {

    /** Get TableInfo, create if doesn't exist **/
    {
      ScopedLock lock(m_mutex);
      if (!m_live_map->get(table->id, table_info)) {
        table_info = new TableInfo(m_master_client, table, schema);
        register_table = true;
      }
    }

    // Verify schema, this will create the Schema object and add it to
    // table_info if it doesn't exist
    verify_schema(table_info, table->generation);

    if (register_table)
      m_live_map->set(table->id, table_info);

    /**
     * Make sure this range is not already loaded
     */
    if (table_info->get_range(range, range_ptr))
      HT_THROW(Error::RANGESERVER_RANGE_ALREADY_LOADED, (String)table->name
               + "[" + range->start_row + ".." + range->end_row + "]");

    /**
     * Lazily create METADATA table pointer
     */
    if (!Global::metadata_table_ptr) {
      ScopedLock lock(m_mutex);
      // double-check locking (works fine on x86 and amd64 but may fail
      // on other archs without using a memory barrier
      if (!Global::metadata_table_ptr)
        Global::metadata_table_ptr = new Table(m_props, m_conn_manager,
            Global::hyperspace_ptr, "METADATA");
    }

    schema = table_info->get_schema();

    /**
     * Take ownership of the range by writing the 'Location' column in the
     * METADATA table, or /hypertable/root{location} attribute of Hyperspace
     * if it is the root range.
     */
    if (!is_root) {

      metadata_key_str = format("%lu:%s", (Lu)table->id, range->end_row);

      /**
       * Take ownership of the range
       */
      mutator_ptr = Global::metadata_table_ptr->create_mutator();

      key.row = metadata_key_str.c_str();
      key.row_len = strlen(metadata_key_str.c_str());
      key.column_family = "Location";
      key.column_qualifier = 0;
      key.column_qualifier_len = 0;
      mutator_ptr->set(key, Global::location.c_str(),
                       Global::location.length());
      mutator_ptr->flush();
    }
    else {  //root
      uint64_t handle;
      HandleCallbackPtr null_callback;
      uint32_t oflags = OPEN_FLAG_READ | OPEN_FLAG_WRITE | OPEN_FLAG_CREATE;

      HT_INFO("Loading root METADATA range");

      try {
        handle = m_hyperspace->open("/hypertable/root", oflags, null_callback);
        m_hyperspace->attr_set(handle, "Location", Global::location.c_str(),
                               Global::location.length());
        m_hyperspace->close(handle);
      }
      catch (Exception &e) {
        HT_ERROR_OUT << "Problem setting attribute 'location' on Hyperspace "
            "file '/hypertable/root'" << HT_END;
        HT_ERROR_OUT << e << HT_END;
        HT_ABORT;
      }

    }

    /**
     * Check for existence of and create, if necessary, range directory (md5 of
     * endrow) under all locality group directories for this table
     */
    {
      assert(*range->end_row != 0);
      md5_string(range->end_row, md5DigestStr);
      md5DigestStr[24] = 0;
      table_dfsdir = (String)"/hypertable/tables/" + table->name;

      foreach(Schema::AccessGroup *ag, schema->get_access_groups()) {
        // notice the below variables are different "range" vs. "table"
        range_dfsdir = table_dfsdir + "/" + ag->name + "/" + md5DigestStr;
        Global::dfs->mkdirs(range_dfsdir);
      }
    }

    range_ptr = new Range(m_master_client, table, schema, range,
                          table_info.get(), range_state);
    /**
     * Create root and/or metadata log if necessary
     */
    if (table->id == 0) {
      if (is_root) {
        Global::log_dfs->mkdirs(Global::log_dir + "/root");
        Global::root_log = new CommitLog(Global::log_dfs, Global::log_dir
                                         + "/root", m_props);
      }
      else if (Global::metadata_log == 0) {
        Global::log_dfs->mkdirs(Global::log_dir + "/metadata");
        Global::metadata_log = new CommitLog(Global::log_dfs,
            Global::log_dir + "/metadata", m_props);
      }
    }

    /**
     * NOTE: The range does not need to be locked in the following replay since
     * it has not been added yet and therefore no one else can find it and
     * concurrently access it.
     */
    if (transfer_log_dir && *transfer_log_dir) {
      CommitLogReaderPtr commit_log_reader_ptr =
          new CommitLogReader(Global::dfs, transfer_log_dir);
      CommitLog *log;
      if (is_root)
        log = Global::root_log;
      else if (table->id == 0)
        log = Global::metadata_log;
      else
        log = Global::user_log;

      if ((error = log->link_log(commit_log_reader_ptr.get())) != Error::OK)
        HT_THROWF(error, "Unable to link transfer log (%s) into commit log(%s)",
                  transfer_log_dir, log->get_log_dir().c_str());

      range_ptr->replay_transfer_log(commit_log_reader_ptr.get());

    }

    table_info->add_range(range_ptr);

    if (Global::range_log)
      Global::range_log->log_range_loaded(*table, *range, *range_state);

    if (cb && (error = cb->response_ok()) != Error::OK) {
      HT_ERRORF("Problem sending OK response - %s", Error::get_text(error));
    }
    else {
      HT_INFOF("Successfully loaded range %s[%s..%s]", table->name,
               range->start_row, range->end_row);
    }
  }
  catch (Hypertable::Exception &e) {
    HT_ERRORF("%s '%s'", Error::get_text(e.code()), e.what());
    if (cb && (error = cb->error(e.code(), e.what())) != Error::OK) {
      HT_ERRORF("Problem sending error response - %s", Error::get_text(error));
    }
  }
}


void
RangeServer::transform_key(ByteString &bskey, DynamicBuffer *dest_bufp,
                           int64_t auto_revision, int64_t *revisionp) {
  size_t len;
  const uint8_t *ptr;

  len = bskey.decode_length(&ptr);

  if (*ptr == Key::AUTO_TIMESTAMP) {
    dest_bufp->ensure( (ptr-bskey.ptr) + len + 9 );
    Serialization::encode_vi32(&dest_bufp->ptr, len+8);
    memcpy(dest_bufp->ptr, ptr, len);
    *dest_bufp->ptr = Key::HAVE_REVISION
        | Key::HAVE_TIMESTAMP | Key::REV_IS_TS;
    dest_bufp->ptr += len;
    Key::encode_ts64(&dest_bufp->ptr, auto_revision);
    *revisionp = auto_revision;
    bskey.ptr = ptr + len;
  }
  else if (*ptr == Key::HAVE_TIMESTAMP) {
    dest_bufp->ensure( (ptr-bskey.ptr) + len + 9 );
    Serialization::encode_vi32(&dest_bufp->ptr, len+8);
    memcpy(dest_bufp->ptr, ptr, len);
    *dest_bufp->ptr = Key::HAVE_REVISION
        | Key::HAVE_TIMESTAMP;
    dest_bufp->ptr += len;
    Key::encode_ts64(&dest_bufp->ptr, auto_revision);
    *revisionp = auto_revision;
    bskey.ptr = ptr + len;
  }
  else {
    HT_ASSERT(!"unknown key control flag");
  }

  return;
}


namespace {

  struct UpdateExtent {
    const uint8_t *base;
    uint64_t len;
  };

  struct SendBackRec {
    int error;
    uint32_t count;
    uint32_t offset;
    uint32_t len;
  };

  struct RangeUpdateInfo {
    RangePtr range_ptr;
    DynamicBuffer *bufp;
    uint64_t offset;
    uint64_t len;
  };

}


void
RangeServer::update(ResponseCallbackUpdate *cb, const TableIdentifier *table,
                    uint32_t count, StaticBuffer &buffer) {
  const uint8_t *mod_ptr, *mod_end;
  String errmsg;
  int error = Error::OK;
  TableInfoPtr table_info;
  int64_t last_revision;
  int64_t latest_range_revision;
  const char *row;
  SplitPredicate split_predicate;
  CommitLogPtr splitlog;
  String end_row;
  bool split_pending;
  SerializedKey key;
  ByteString value;
  bool a_locked = false;
  bool b_locked = false;
  vector<SendBackRec> send_back_vector;
  SendBackRec send_back;
  uint32_t total_added = 0;
  uint32_t split_added = 0;
  std::vector<RangeUpdateInfo> range_vector;
  DynamicBuffer root_buf;
  DynamicBuffer go_buf;
  DynamicBuffer *split_bufp = 0;
  std::vector<DynamicBufferPtr> split_bufs;
  DynamicBuffer *cur_bufp;
  uint32_t misses = 0;
  RangeUpdateInfo rui;
  std::set<Range *> reference_set;
  std::pair<std::set<Range *>::iterator, bool> reference_set_state;

  // Pre-allocate the go_buf - each key could expand by 8 or 9 bytes,
  // if auto-assigned (8 for the ts or rev and maybe 1 for possible
  // increase in vint length)
  const uint32_t encoded_table_len = table->encoded_length();
  go_buf.reserve(encoded_table_len + buffer.size + (count * 9));
  table->encode(&go_buf.ptr);

  HT_DEBUG_OUT <<"Update:\n"<< *table << HT_END;

  if (!m_replay_finished)
    wait_for_recovery_finish();

  // Global commit log is only available after local recovery
  int64_t auto_revision = Global::user_log->get_timestamp();

  // TODO: Sanity check mod data (checksum validation)

  try {
    // Fetch table info
    if (!m_live_map->get(table->id, table_info)) {
      StaticBuffer ext(new uint8_t [16], 16);
      uint8_t *ptr = ext.base;
      encode_i32(&ptr, Error::RANGESERVER_TABLE_NOT_FOUND);
      encode_i32(&ptr, count);
      encode_i32(&ptr, 0);
      encode_i32(&ptr, buffer.size);
      HT_ERRORF("Unable to find table info for table '%s'", table->name);
      if ((error = cb->response(ext)) != Error::OK)
        HT_ERRORF("Problem sending OK response - %s", Error::get_text(error));
      return;
    }

    // verify schema
    verify_schema(table_info, table->generation);

    mod_end = buffer.base + buffer.size;
    mod_ptr = buffer.base;

    m_update_mutex_a.lock();
    a_locked = true;

    memset(&send_back, 0, sizeof(send_back));

    while (mod_ptr < mod_end) {
      key.ptr = mod_ptr;
      row = key.row();

      // If the row key starts with '\0' then the buffer is probably
      // corrupt, so mark the remaing key/value pairs as bad
      if (*row == 0) {
        send_back.error = Error::BAD_KEY;
        send_back.count = count;  // fix me !!!!
        send_back.offset = mod_ptr - buffer.base;
        send_back.len = mod_end - mod_ptr;
        send_back_vector.push_back(send_back);
        memset(&send_back, 0, sizeof(send_back));
        mod_ptr = mod_end;
        continue;
      }

      // Look for containing range, add to stop mods if not found
      if (!table_info->find_containing_range(row, rui.range_ptr)) {
        if (send_back.error != Error::RANGESERVER_OUT_OF_RANGE
            && send_back.count > 0) {
          send_back_vector.push_back(send_back);
          memset(&send_back, 0, sizeof(send_back));
        }
        if (send_back.count == 0) {
          send_back.error = Error::RANGESERVER_OUT_OF_RANGE;
          send_back.offset = mod_ptr - buffer.base;
        }
        key.next(); // skip key
        key.next(); // skip value;
        mod_ptr = key.ptr;
        send_back.count++;
        continue;
      }

      // See if range has some other error preventing it from receiving updates
      if ((error = rui.range_ptr->get_error()) != Error::OK) {
        if (send_back.error != error && send_back.count > 0) {
          send_back_vector.push_back(send_back);
          memset(&send_back, 0, sizeof(send_back));
        }
        if (send_back.count == 0) {
          send_back.error = error;
          send_back.offset = mod_ptr - buffer.base;
        }
        key.next(); // skip key
        key.next(); // skip value;
        mod_ptr = key.ptr;
        send_back.count++;
        continue;
      }

      if (send_back.count > 0) {
        send_back.len = (mod_ptr - buffer.base) - send_back.offset;
        send_back_vector.push_back(send_back);
        memset(&send_back, 0, sizeof(send_back));
      }

      /** Increment update count (block if maintenance in progress) **/
      reference_set_state = reference_set.insert(rui.range_ptr.get());
      if (reference_set_state.second)
        rui.range_ptr->increment_update_counter();

      // Make sure range didn't just shrink
      if (!rui.range_ptr->belongs(row)) {
        if (reference_set_state.second) {
          rui.range_ptr->decrement_update_counter();
          reference_set.erase(rui.range_ptr.get());
        }
        continue;
      }

      end_row = rui.range_ptr->end_row();

      /** Fetch range split information **/
      split_pending = rui.range_ptr->get_split_info(split_predicate, splitlog,
                                                    &latest_range_revision);
      bool in_split_off_region = false;

      // Check for clock skew
      {
        ByteString tmp_key;
        const uint8_t *tmp_ptr;
        int64_t difference, tmp_timestamp;
        tmp_key.ptr = key.ptr;
        tmp_key.decode_length(&tmp_ptr);
        if ((*tmp_ptr & Key::HAVE_REVISION) == 0) {
          if (latest_range_revision > TIMESTAMP_NULL
              && auto_revision < latest_range_revision) {
            tmp_timestamp = Global::user_log->get_timestamp();
            if (tmp_timestamp > auto_revision)
              auto_revision = tmp_timestamp;
            if (auto_revision < latest_range_revision) {
              difference = (int32_t)((latest_range_revision - auto_revision)
                            / 1000LL);
              if (difference > m_max_clock_skew)
                HT_THROWF(Error::RANGESERVER_CLOCK_SKEW,
                          "Clocks skew of %lld microseconds exceeds maximum "
                          "(%lld) range=%s", (Lld)difference,
                          (Lld)m_max_clock_skew,
                          rui.range_ptr->get_name().c_str());
            }
          }
        }
      }

      if (split_pending) {
        split_bufp = new DynamicBuffer();
        split_bufs.push_back(split_bufp);
        split_bufp->reserve(encoded_table_len);
        table->encode(&split_bufp->ptr);
      }
      else
        split_bufp = 0;

      if (rui.range_ptr->is_root())
        cur_bufp = &root_buf;
      else
        cur_bufp = &go_buf;

      if (cur_bufp->ptr == 0) {
        cur_bufp->reserve(encoded_table_len);
        table->encode(&cur_bufp->ptr);
      }

      rui.bufp = cur_bufp;
      rui.offset = cur_bufp->fill();

      while (mod_ptr < mod_end && (end_row == ""
             || (strcmp(row, end_row.c_str()) <= 0))) {

        if (split_pending) {

          if (split_predicate.split_off(row)) {
            if (!in_split_off_region) {
              rui.len = cur_bufp->fill() - rui.offset;
              if (rui.len)
                range_vector.push_back(rui);
              cur_bufp = split_bufp;
              rui.bufp = cur_bufp;
              rui.offset = cur_bufp->fill();
              in_split_off_region = true;
            }
            split_added++;
          }
          else {
            if (in_split_off_region) {
              rui.len = cur_bufp->fill() - rui.offset;
              if (rui.len)
                range_vector.push_back(rui);
              cur_bufp = &go_buf;
              rui.bufp = cur_bufp;
              rui.offset = cur_bufp->fill();
              in_split_off_region = false;
            }
          }
        }

        // This will transform keys that need to be assigned a
        // timestamp and/or revision number by re-writing the key
        // with the added timestamp and/or revision tacked on to the end
        transform_key(key, cur_bufp, ++auto_revision, &last_revision);

        // Validate revision number
        if (last_revision < latest_range_revision) {
          if (last_revision != auto_revision)
            HT_THROWF(Error::RANGESERVER_REVISION_ORDER_ERROR,
                      "Supplied revision (%lld) is less than most recently "
                      "seen revision (%lld) for range %s",
                      (Lld)last_revision, (Lld)latest_range_revision,
                      rui.range_ptr->get_name().c_str());
        }

        // Now copy the value (with sanity check)
        mod_ptr = key.ptr;
        key.next(); // skip value
        HT_ASSERT(key.ptr <= mod_end);
        cur_bufp->add(mod_ptr, key.ptr-mod_ptr);
        mod_ptr = key.ptr;

        total_added++;

        if (mod_ptr < mod_end)
          row = key.row();
      }

      rui.len = cur_bufp->fill() - rui.offset;
      if (rui.len)
        range_vector.push_back(rui);
      rui.range_ptr = 0;
      rui.bufp = 0;

      // if there were split-off updates, write the split log entry
      if (split_bufp && split_bufp->fill() > encoded_table_len) {
        if ((error = splitlog->write(*split_bufp, last_revision)) != Error::OK)
          HT_THROWF(error, "Problem writing %d bytes to split log",
                    (int)split_bufp->fill());
        splitlog = 0;
      }
    }

    HT_DEBUGF("Added %d (%d split off) updates to '%s'", total_added,
              split_added, table->name);

    if (send_back.count > 0) {
      send_back.len = (mod_ptr - buffer.base) - send_back.offset;
      send_back_vector.push_back(send_back);
      memset(&send_back, 0, sizeof(send_back));
    }

    m_update_mutex_b.lock();
    b_locked = true;

    m_update_mutex_a.unlock();
    a_locked = false;

    /**
     * Commit ROOT mutations
     */
    if (root_buf.fill() > encoded_table_len) {
      if ((error = Global::root_log->write(root_buf, last_revision))
          != Error::OK)
        HT_THROWF(error, "Problem writing %d bytes to ROOT commit log",
                  (int)root_buf.fill());
    }

    /**
     * Commit valid (go) mutations
     */
    if (go_buf.fill() > encoded_table_len) {
      CommitLog *log = (table->id == 0) ? Global::metadata_log
                                        : Global::user_log;
      if ((error = log->write(go_buf, last_revision)) != Error::OK)
        HT_THROWF(error, "Problem writing %d bytes to commit log (%s)",
                  (int)go_buf.fill(), log->get_log_dir().c_str());
    }

    for (size_t rangei=0; rangei<range_vector.size(); rangei++) {

      /**
       * Apply the modifications
       */
      range_vector[rangei].range_ptr->lock();
      {
        Key key_comps;
        uint8_t *ptr = range_vector[rangei].bufp->base
            + range_vector[rangei].offset;
        uint8_t *end = ptr + range_vector[rangei].len;
        while (ptr < end) {
          key.ptr = ptr;
          key_comps.load(key);
          ptr += key_comps.length;
          value.ptr = ptr;
          ptr += value.length();
          if ((error = range_vector[rangei].range_ptr->add(key_comps, value))
              != Error::OK)
            HT_WARNF("Range::add() - %s", Error::get_text(error));
        }
      }
      range_vector[rangei].range_ptr->unlock();

      /**
       * Split and Compaction processing
       */
      if (!range_vector[rangei].range_ptr->maintenance_in_progress()) {
        Range::CompactionPriorityData priority_data_vec;
        std::vector<AccessGroup *> compactions;
        uint64_t disk_usage = 0;

        range_vector[rangei].range_ptr->get_compaction_priority_data(
            priority_data_vec);
        for (size_t i=0; i<priority_data_vec.size(); i++) {
          disk_usage += priority_data_vec[i].disk_used;
          if (!priority_data_vec[i].in_memory && priority_data_vec[i].mem_used
              >= (uint32_t)Global::access_group_max_mem)
            compactions.push_back(priority_data_vec[i].ag);
        }

        if (!range_vector[rangei].range_ptr->is_root() &&
            (disk_usage > range_vector[rangei].range_ptr->get_size_limit() ||
             (Global::range_metadata_max_bytes && table->id == 0
             && disk_usage > Global::range_metadata_max_bytes))) {
          if (!range_vector[rangei].range_ptr->test_and_set_maintenance())
            Global::maintenance_queue->add(new MaintenanceTaskSplit(
                range_vector[rangei].range_ptr));
        }
        else if (!compactions.empty()) {
          if (!range_vector[rangei].range_ptr->test_and_set_maintenance()) {
            for (size_t i=0; i<compactions.size(); i++)
              compactions[i]->set_compaction_bit();
            Global::maintenance_queue->add(new MaintenanceTaskCompaction(
                range_vector[rangei].range_ptr, false));
          }
        }
      }
    }

    if (Global::verbose && misses)
      HT_INFOF("Sent back %d updates because out-of-range", misses);

    error = Error::OK;
  }
  catch (Exception &e) {
    HT_ERRORF("Exception caught: %s", Error::get_text(e.code()));
    error = e.code();
    errmsg = e.what();
  }

  // decrement usage counters for all referenced ranges
  foreach(Range *range, reference_set)
    range->decrement_update_counter();

  if (b_locked)
    m_update_mutex_b.unlock();
  else if (a_locked)
    m_update_mutex_a.unlock();

  m_bytes_loaded += buffer.size;

  if (error == Error::OK) {
    /**
     * Send back response
     */
    if (!send_back_vector.empty()) {
      StaticBuffer ext(new uint8_t [send_back_vector.size() * 16],
                       send_back_vector.size() * 16);
      uint8_t *ptr = ext.base;
      for (size_t i=0; i<send_back_vector.size(); i++) {
        encode_i32(&ptr, send_back_vector[i].error);
        encode_i32(&ptr, send_back_vector[i].count);
        encode_i32(&ptr, send_back_vector[i].offset);
        encode_i32(&ptr, send_back_vector[i].len);
        HT_INFOF("omega Sending back error %x, count %d, offset %d, len %d",
                 send_back_vector[i].error, send_back_vector[i].count,
                 send_back_vector[i].offset, send_back_vector[i].len);
      }
      if ((error = cb->response(ext)) != Error::OK)
        HT_ERRORF("Problem sending OK response - %s", Error::get_text(error));
    }
    else {
      if ((error = cb->response_ok()) != Error::OK)
        HT_ERRORF("Problem sending OK response - %s", Error::get_text(error));
    }
  }
  else {
    HT_ERRORF("%s '%s'", Error::get_text(error), errmsg.c_str());
    if ((error = cb->error(error, errmsg)) != Error::OK)
      HT_ERRORF("Problem sending error response - %s", Error::get_text(error));
  }
}


void
RangeServer::drop_table(ResponseCallback *cb, const TableIdentifier *table) {
  TableInfoPtr table_info;
  std::vector<RangePtr> range_vector;
  String metadata_prefix;
  String metadata_key;
  TableMutatorPtr mutator_ptr;
  KeySpec key;

  HT_DEBUG_OUT << table->name << HT_END;

  if (!m_replay_finished)
    wait_for_recovery_finish();

  // create METADATA table mutator for clearing 'Location' columns
  mutator_ptr = Global::metadata_table_ptr->create_mutator();

  // initialize key structure
  memset(&key, 0, sizeof(key));
  key.column_family = "Location";

  try {

     // For each range in dropped table, Set the 'drop' bit and clear
    // the 'Location' column of the corresponding METADATA entry
    if (m_live_map->remove(table->id, table_info)) {
      metadata_prefix = String("") + table_info->get_id() + ":";
      table_info->get_range_vector(range_vector);
      for (size_t i=0; i<range_vector.size(); i++) {
        range_vector[i]->drop();
        metadata_key = metadata_prefix + range_vector[i]->end_row();
        key.row = metadata_key.c_str();
        key.row_len = metadata_key.length();
        mutator_ptr->set(key, "!", 1);
      }
      range_vector.clear();
    }
    else {
      HT_ERRORF("drop_table '%s' id=%u - table not found", table->name,
                table->id);
    }
    mutator_ptr->flush();
  }
  catch (Hypertable::Exception &e) {
    HT_ERRORF("Problem clearing 'Location' columns of METADATA - %s",
              Error::get_text(e.code()));
    cb->error(e.code(), "Problem clearing 'Location' columns of METADATA");
    return;
  }

  // write range transaction entry
  if (Global::range_log)
    Global::range_log->log_drop_table(*table);

  HT_INFOF("Successfully dropped table '%s'", table->name);

  cb->response_ok();
}


void RangeServer::dump_stats(ResponseCallback *cb) {
  std::vector<TableInfoPtr> table_vec;
  std::vector<RangePtr> range_vec;

  HT_DEBUG("dump_stats");

  m_live_map->get_all(table_vec);

  for (size_t i=0; i<table_vec.size(); i++) {
    range_vec.clear();
    table_vec[i]->get_range_vector(range_vec);
    for (size_t i=0; i<range_vec.size(); i++)
      range_vec[i]->dump_stats();
  }
  cb->response_ok();
}

void RangeServer::get_statistics(ResponseCallbackGetStatistics *cb) {
  std::vector<TableInfoPtr> table_vec;
  std::vector<RangePtr> range_vec;
  RangeServerStat stat;

  HT_DEBUG("get_statistics");

  m_live_map->get_all(table_vec);

  for (size_t i=0; i<table_vec.size(); i++) {
    range_vec.clear();
    table_vec[i]->get_range_vector(range_vec);
    for (size_t i=0; i<range_vec.size(); i++) {
      RangeStat rstat;
      range_vec[i]->get_statistics(&rstat);
      stat.range_stats.push_back(rstat);
    }
  }

  StaticBuffer ext(stat.encoded_length());
  uint8_t *bufp = ext.base;
  stat.encode(&bufp);

  cb->response(ext);
}


void RangeServer::replay_begin(ResponseCallback *cb, uint16_t group) {
  String replay_log_dir = format("/hypertable/servers/%s/log/replay",
                                 Global::location.c_str());

  m_replay_group = group;

  HT_INFOF("replay_start group=%d", m_replay_group);

  m_replay_log = 0;

  m_replay_map->clear_ranges();

  /**
   * Remove old replay log directory
   */
  try { Global::log_dfs->rmdir(replay_log_dir); }
  catch (Exception &e) {
    HT_ERRORF("Problem removing replay log directory: %s", e.what());
    cb->error(e.code(), format("Problem removing replay log directory: %s",
              e.what()));
    return;
  }

  /**
   * Create new replay log directory
   */
  try { Global::log_dfs->mkdirs(replay_log_dir); }
  catch (Exception &e) {
    HT_ERRORF("Problem creating replay log directory: %s ", e.what());
    cb->error(e.code(), format("Problem creating replay log directory: %s",
              e.what()));
    return;
  }

  m_replay_log = new CommitLog(Global::log_dfs, replay_log_dir, m_props);

  cb->response_ok();
}


void
RangeServer::replay_load_range(ResponseCallback *cb,
    const TableIdentifier *table, const RangeSpec *range,
    const RangeState *range_state) {
  int error = Error::OK;
  SchemaPtr schema;
  TableInfoPtr table_info;
  RangePtr range_ptr;
  bool register_table = false;

  HT_DEBUG_OUT<< "replay_load_range\n"<< *table << *range << HT_END;

  try {

    /** Get TableInfo from replay map, or copy it from live map, or create if
     * doesn't exist **/
    if (!m_replay_map->get(table->id, table_info)) {
      if (!m_live_map->get(table->id, table_info))
        table_info = new TableInfo(m_master_client, table, schema);
      else
        table_info = table_info->create_shallow_copy();
      register_table = true;
    }

    // Verify schema, this will create the Schema object and add it to
    // table_info if it doesn't exist
    verify_schema(table_info, table->generation);

    if (register_table)
      m_replay_map->set(table->id, table_info);

    /**
     * Make sure this range is not already loaded
     */
    if (table_info->get_range(range, range_ptr))
      HT_THROWF(Error::RANGESERVER_RANGE_ALREADY_LOADED, "%s[%s..%s]",
                table->name, range->start_row, range->end_row);

    /**
     * Lazily create METADATA table pointer
     */
    if (!Global::metadata_table_ptr) {
      ScopedLock lock(m_mutex);
      Global::metadata_table_ptr = new Table(m_props, m_conn_manager,
          Global::hyperspace_ptr, "METADATA");
    }

    schema = table_info->get_schema();

    range_ptr = new Range(m_master_client, table, schema, range,
                          table_info.get(), range_state);

    range_ptr->recovery_initialize();

    table_info->add_range(range_ptr);

    if (Global::range_log)
      Global::range_log->log_range_loaded(*table, *range, *range_state);

    if (cb && (error = cb->response_ok()) != Error::OK) {
      HT_ERRORF("Problem sending OK response - %s", Error::get_text(error));
    }
    else {
      HT_INFOF("Successfully replay loaded range %s[%s..%s]", table->name,
               range->start_row, range->end_row);
    }

  }
  catch (Hypertable::Exception &e) {
    HT_ERRORF("%s '%s'", Error::get_text(error), e.what());
    if (cb && (error = cb->error(e.code(), e.what())) != Error::OK) {
      HT_ERRORF("Problem sending error response - %s", Error::get_text(error));
    }
  }
}



void
RangeServer::replay_update(ResponseCallback *cb, const uint8_t *data,
                           size_t len) {
  TableIdentifier table_identifier;
  TableInfoPtr table_info;
  SerializedKey serkey;
  ByteString bsvalue;
  Key key;
  const uint8_t *ptr = data;
  const uint8_t *end_ptr = data + len;
  const uint8_t *block_end_ptr;
  uint32_t block_size;
  size_t remaining = len;
  const char *row;
  String err_msg;
  int64_t revision;
  RangePtr range_ptr;
  String end_row;
  int error;

  //HT_DEBUGF("replay_update - length=%ld", len);

  try {

    while (ptr < end_ptr) {

      // decode key/value block size + revision
      block_size = decode_i32(&ptr, &remaining);
      revision = decode_i64(&ptr, &remaining);

      if (m_replay_log) {
        DynamicBuffer dbuf(0, false);
        dbuf.base = (uint8_t *)ptr;
        dbuf.ptr = dbuf.base + remaining;

        if ((error = m_replay_log->write(dbuf, revision)) != Error::OK)
          HT_THROW(error, "");
      }

      // decode table identifier
      table_identifier.decode(&ptr, &remaining);

      if (block_size > remaining)
        HT_THROWF(Error::MALFORMED_REQUEST, "Block (size=%lu) exceeds EOM",
                  (Lu)block_size);

      block_end_ptr = ptr + block_size;

      // Fetch table info
      if (!m_replay_map->get(table_identifier.id, table_info))
        HT_THROWF(Error::RANGESERVER_RANGE_NOT_FOUND, "Unable to find "
                  "table info for table name='%s' id=%lu",
                  table_identifier.name, (Lu)table_identifier.id);

      while (ptr < block_end_ptr) {

        row = SerializedKey(ptr).row();

        // Look for containing range, add to stop mods if not found
        if (!table_info->find_containing_range(row, range_ptr))
          HT_THROWF(Error::RANGESERVER_RANGE_NOT_FOUND, "Unable to find "
                    "range for row '%s'", row);

        end_row = range_ptr->end_row();
        serkey.ptr = ptr;

        while (ptr < block_end_ptr
            && (end_row == "" || (strcmp(row, end_row.c_str()) <= 0))) {

          // extract the key
          ptr += serkey.length();

          if (ptr > end_ptr)
            HT_THROW(Error::REQUEST_TRUNCATED, "Problem decoding key");

          bsvalue.ptr = ptr;
          ptr += bsvalue.length();

          if (ptr > end_ptr)
            HT_THROW(Error::REQUEST_TRUNCATED, "Problem decoding value");

          key.load(serkey);

          range_ptr->lock();
          HT_ASSERT(range_ptr->add(key, bsvalue) == Error::OK);
          range_ptr->unlock();
          serkey.ptr = ptr;

          if (ptr < block_end_ptr)
            row = serkey.row();
        }
      }
    }

  }
  catch (Exception &e) {

    if (e.code() == Error::RANGESERVER_RANGE_NOT_FOUND)
      HT_INFO_OUT << e << HT_END;
    else
      HT_ERROR_OUT << e << HT_END;

    if (cb) {
      cb->error(e.code(), format("%s - %s", e.what(),
                Error::get_text(e.code())));
      return;
    }
    HT_THROW(e.code(), e.what());
  }

  if (cb)
    cb->response_ok();
}


void RangeServer::replay_commit(ResponseCallback *cb) {
  int error;

  HT_INFO("replay_commit");

  try {
    CommitLog *log = 0;
    std::vector<RangePtr> rangev;

    if (m_replay_group == RangeServerProtocol::GROUP_METADATA_ROOT)
      log = Global::root_log;
    else if (m_replay_group == RangeServerProtocol::GROUP_METADATA)
      log = Global::metadata_log;
    else if (m_replay_group == RangeServerProtocol::GROUP_USER)
      log = Global::user_log;

    if ((error = log->link_log(m_replay_log.get())) != Error::OK)
      HT_THROW(error, String("Problem linking replay log (")
               + m_replay_log->get_log_dir() + ") into commit log ("
               + log->get_log_dir() + ")");

    // Perform any range specific post-replay tasks
    m_replay_map->get_range_vector(rangev);
    foreach(RangePtr &range_ptr, rangev)
      range_ptr->recovery_finalize();

    m_live_map->merge(m_replay_map);

  }
  catch (Hypertable::Exception &e) {
    HT_ERRORF("%s - %s", e.what(), Error::get_text(e.code()));
    if (cb) {
      cb->error(e.code(), e.what());
      return;
    }
    HT_THROW(e.code(), e.what());
  }

  if (cb)
    cb->response_ok();
}



void
RangeServer::drop_range(ResponseCallback *cb, const TableIdentifier *table,
                        const RangeSpec *range) {
  TableInfoPtr table_info;
  RangePtr range_ptr;

  HT_DEBUG_OUT << "drop_range\n"<< *table << *range << HT_END;

  /** Get TableInfo **/
  if (!m_live_map->get(table->id, table_info)) {
    cb->error(Error::RANGESERVER_RANGE_NOT_FOUND,
              String("No ranges loaded for table '") + table->name + "'");
    return;
  }

  /** Remove the range **/
  if (!table_info->remove_range(range, range_ptr)) {
    cb->error(Error::RANGESERVER_RANGE_NOT_FOUND, format("%s[%s..%s]",
              table->name, range->start_row, range->end_row));
    return;
  }

  cb->response_ok();
}

void RangeServer::shutdown(ResponseCallback *cb) {
  std::vector<TableInfoPtr> table_vec;
  std::vector<RangePtr> range_vec;

  (void)cb;

  Global::maintenance_queue->stop();

  // block updates
  m_update_mutex_a.lock();
  m_update_mutex_b.lock();

  // get the tables
  m_live_map->get_all(table_vec);

  // add all ranges into the range vector
  for (size_t i=0; i<table_vec.size(); i++)
    table_vec[i]->get_range_vector(range_vec);

  // increment the update counters
  for (size_t i=0; i<range_vec.size(); i++)
    range_vec[i]->increment_update_counter();

  m_hyperspace = 0;

  if (Global::range_log)
    Global::range_log->close();

  if (Global::root_log)
    Global::root_log->close();

  if (Global::metadata_log)
    Global::metadata_log->close();

  if (Global::user_log)
    Global::user_log->close();

}



void RangeServer::verify_schema(TableInfoPtr &table_info, int generation) {
  DynamicBuffer valbuf;
  HandleCallbackPtr null_handle_callback;
  uint64_t handle;
  SchemaPtr schema = table_info->get_schema();

  if (schema.get() == 0 || schema->get_generation() < generation) {
    String tablefile = (String)"/hypertable/tables/" + table_info->get_name();

    handle = m_hyperspace->open(tablefile.c_str(), OPEN_FLAG_READ,
                                    null_handle_callback);

    m_hyperspace->attr_get(handle, "schema", valbuf);

    m_hyperspace->close(handle);

    schema = Schema::new_instance((char *)valbuf.base, valbuf.fill(), true);

    if (!schema->is_valid())
      HT_THROW(Error::RANGESERVER_SCHEMA_PARSE_ERROR,
               (String)"Schema Parse Error for table '"
               + table_info->get_name() + "' : " + schema->get_error_string());

    table_info->update_schema(schema);

    // Generation check ...
    if (schema->get_generation() < generation)
      HT_THROW(Error::RANGESERVER_GENERATION_MISMATCH,
               (String)"Fetched Schema generation for table '"
               + table_info ->get_name() + "' is " + schema->get_generation()
               + " but supplied is " + generation);
  }

}



void RangeServer::do_maintenance() {
  struct timeval tval;

  /**
   * Purge expired scanners
   */
  Global::scanner_map.purge_expired(m_scanner_ttl);

  /**
   * Schedule log cleanup
   */
  gettimeofday(&tval, 0);
  if ((tval.tv_sec - m_last_commit_log_clean)
      >= (int)(m_timer_interval*4)/5000) {
    // schedule log cleanup
    Global::maintenance_queue->add(new MaintenanceTaskLogCleanup(this));
    m_last_commit_log_clean = tval.tv_sec;
  }

  HT_INFOF("Memory Usage: %llu bytes", (Llu)Global::memory_tracker.balance());

}

namespace {

  struct LtPriorityData {
    bool operator()(const AccessGroup::CompactionPriorityData &pd1,
                    const AccessGroup::CompactionPriorityData &pd2) const {
      return pd1.log_space_pinned >= pd2.log_space_pinned;
    }
  };


}


void RangeServer::log_cleanup() {
  std::vector<TableInfoPtr> table_vec;
  std::vector<RangePtr> range_vec;
  uint64_t prune_threshold;
  size_t first_user_table = 0;

  if (!m_replay_finished)
    wait_for_recovery_finish();

  m_live_map->get_all(table_vec);

  if (table_vec.empty())
    return;

  /**
   * If we've got METADATA ranges, process them first
   */
  if (table_vec[0]->get_id() == 0 && Global::metadata_log) {
    first_user_table = 1;
    table_vec[0]->get_range_vector(range_vec);
    // skip root
    if (!range_vec.empty() && range_vec[0]->end_row() == Key::END_ROOT_ROW)
      range_vec.erase(range_vec.begin());
    schedule_log_cleanup_compactions(range_vec, Global::metadata_log,
                                     Global::log_prune_threshold_min);
  }

  range_vec.clear();
  for (size_t i=first_user_table; i<table_vec.size(); i++)
    table_vec[i]->get_range_vector(range_vec);

  // compute prune threshold (MB/s * prune_max)
  prune_threshold = (uint64_t)((((double)m_bytes_loaded
      / (double)m_timer_interval) / 1000.0)
      * (double)Global::log_prune_threshold_max);
  if (prune_threshold < Global::log_prune_threshold_min)
    prune_threshold = Global::log_prune_threshold_min;
  else if (prune_threshold > Global::log_prune_threshold_max)
    prune_threshold = Global::log_prune_threshold_max;

  HT_INFOF("Cleaning log (threshold=%llu)", (Llu)prune_threshold);

  schedule_log_cleanup_compactions(range_vec, Global::user_log,
                                   prune_threshold);
  m_bytes_loaded = 0;
}


void
RangeServer::schedule_log_cleanup_compactions(std::vector<RangePtr> &range_vec,
    CommitLog *log, uint64_t prune_threshold) {
  Range::CompactionPriorityData priority_data_vec;
  LogFragmentPriorityMap log_frag_map;
  int64_t revision, earliest_cached_revision = TIMESTAMP_MAX;

  // Load up a vector of compaction priority data
  for (size_t i=0; i<range_vec.size(); i++) {
    size_t start = priority_data_vec.size();
    range_vec[i]->get_compaction_priority_data(priority_data_vec);
    for (size_t j=start; j<priority_data_vec.size(); j++) {
      priority_data_vec[j].user_data = (void *)i;
      if ((revision = priority_data_vec[j].ag->get_earliest_cached_revision())
          != TIMESTAMP_NULL) {
        if (revision < earliest_cached_revision)
          earliest_cached_revision = revision;
      }
    }
  }

  log->load_fragment_priority_map(log_frag_map);

  /**
   * Determine which AGs need compaction for the sake of
   * garbage collecting commit log fragments
   */
  for (size_t i=0; i<priority_data_vec.size(); i++) {

    if (priority_data_vec[i].earliest_cached_revision == TIMESTAMP_NULL)
      continue;

    LogFragmentPriorityMap::iterator map_iter =
        log_frag_map.lower_bound(priority_data_vec[i].earliest_cached_revision);

    // this should never happen
    if (map_iter == log_frag_map.end())
      continue;

    if ((*map_iter).second.cumulative_size > prune_threshold) {
      if (priority_data_vec[i].mem_used > 0)
        priority_data_vec[i].ag->set_compaction_bit();
      size_t rangei = (size_t)priority_data_vec[i].user_data;
      if (!range_vec[rangei]->test_and_set_maintenance()) {
        Global::maintenance_queue->add(new MaintenanceTaskCompaction(
                                       range_vec[rangei], false));
      }
    }
  }

  // Purge the commit log
  log->purge(earliest_cached_revision);

}


uint64_t RangeServer::get_timer_interval() {
  return m_timer_interval;
}


void RangeServer::wait_for_recovery_finish() {
  ScopedLock lock(m_mutex);
  while (!m_replay_finished) {
    HT_INFO_OUT << "Waiting for recovery to complete..." << HT_END;
    m_replay_finished_cond.wait(lock);
  }
}


void
RangeServer::wait_for_recovery_finish(const TableIdentifier *table,
                                      const RangeSpec *range) {
  ScopedLock lock(m_mutex);
  if (table->id == 0) {
    if (!strcmp(range->end_row, Key::END_ROOT_ROW)) {
      while (!m_root_replay_finished) {
        HT_INFO_OUT << "Waiting for ROOT recovery to complete..." << HT_END;
        m_root_replay_finished_cond.wait(lock);
      }
    }
    else {
      while (!m_metadata_replay_finished) {
        HT_INFO_OUT << "Waiting for METADATA recovery to complete..." << HT_END;
        m_metadata_replay_finished_cond.wait(lock);
      }
    }
  }
  else {
    while (!m_replay_finished) {
      HT_INFO_OUT << "Waiting for recovery to complete..." << HT_END;
      m_replay_finished_cond.wait(lock);
    }
  }
}
