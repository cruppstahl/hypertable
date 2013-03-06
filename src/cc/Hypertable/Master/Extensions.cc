/**
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 3
 * of the License.
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

#include "Hyperspace/Session.h"

#include "Extensions.h"
#include "OperationAlterTable.h"
#include "OperationCreateTable.h"


namespace Hypertable {

using namespace Hyperspace;

PropertiesPtr Extensions::ms_props;

void
Extensions::initialize(PropertiesPtr &props) {
  ms_props = props;
}

void
Extensions::validate_create_table_schema(SchemaPtr &schema) {
  // nop
}

void
Extensions::validate_alter_table_schema(SchemaPtr &schema) {
  // nop
}

bool
Extensions::alter_table_extension(OperationAlterTable *op,
            const String &schema_string, const String &table_name,
            const String &table_id) {
  return true;
}

bool
Extensions::create_table_extension(OperationCreateTable *op,
            const String &schema_string, const String &table_name,
            TableIdentifierManaged &table_id) {
  return true;
}


} // namespace Hypertable
