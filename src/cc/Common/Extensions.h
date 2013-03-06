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
#include "Common/HashMap.h"

#ifndef HYPERTABLE_EXTENSIONS_H
#define HYPERTABLE_EXTENSIONS_H

namespace Hypertable {

class Extensions {
  public:
    typedef hash_map<int, const char *> TextMap;

    // adds error code to the text map with descriptive error strings
    static void build_text_map(TextMap *map);

    // adds configuration options
    static void add_configuration();
};

} // namespace Hypertable

#endif // HYPERTABLE_EXTENSIONS_H
