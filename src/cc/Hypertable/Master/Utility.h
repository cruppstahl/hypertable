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

#ifndef HYPERTABLE_UTILITY_H
#define HYPERTABLE_UTILITY_H

#include "Common/StringExt.h"

#include "Hypertable/Lib/Types.h"

#include "Context.h"

namespace Hypertable {

  namespace Utility {

    extern void get_table_server_set(Context *context, const String &id, StringSet &servers);
    extern bool table_exists(Context *context, const String &name, String &id);
    extern bool table_exists(Context *context, const String &id);
    extern void verify_table_name_availability(Context *context, const String &name, String &id);
    extern void create_table_in_hyperspace(Context *context, const String &name,
                                           const String &schema_str, TableIdentifierManaged *table);
    extern void prepare_index(Context *context, const String &name,
                              const String &schema_str, bool qualifier,
                              String &index_name, String &index_schema);
    extern void create_table_write_metadata(Context *context, TableIdentifier *table);
    extern bool next_available_server(Context *context, String &location);
    extern void create_table_load_range(Context *context, const String &location,
                                        TableIdentifier *table, RangeSpec &range,
                                        bool needs_compaction);
    extern int64_t range_hash_code(const TableIdentifier &table, const RangeSpec &range, const String &qualifier);
    extern String range_hash_string(const TableIdentifier &table, const RangeSpec &range, const String &qualifier);
    extern String root_range_location(Context *context);

  } // namespace Utility

} // namespace Hypertable

#endif // HYPERTABLE_UTILITY_H
