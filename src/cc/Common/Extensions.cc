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
#include "Common/Config.h"

#include "Extensions.h"


namespace Hypertable {

typedef hash_map<int, const char *> TextMap;

void
Extensions::build_text_map(TextMap *map) {
  /* usage example:
  (*map)[ERROR_CODE] = "THIS is a help text";
  */
}

void
Extensions::add_configuration() {
  /* usage example:
  using namespace Property;

  Config::file_desc().add_options()
          ("param1", i32()->default_value(0), "help text 1")
          ("param2", i32()->default_value(0), "help text 2")
        ;
  Config::alias("param1", "param2");
  */
}

} // namespace Hypertable
