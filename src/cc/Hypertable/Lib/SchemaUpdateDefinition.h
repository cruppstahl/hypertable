/** -*- c++ -*-
 * Copyright (C) 2007-2012 Hypertable, Inc.
 *
 * This file is part of Hypertable.
 *
 * Hypertable is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 3 of the
 * License.
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

#include "Hypertable/Lib/MetaLogDefinition.h"
#include "Hypertable/Lib/MetaLogEntity.h"

#include "SchemaUpdateEntities.h"

namespace Hypertable {

class SchemaUpdateDefinition : public MetaLog::Definition
{
  public:
    SchemaUpdateDefinition(const char *backup_name)
      : MetaLog::Definition(backup_name) {
    }

    virtual uint16_t version() { return 1; }

    virtual bool supported_version(uint16_t ver) { return (ver == 1); }

    virtual const char *name() { return "schema"; }

    virtual MetaLog::Entity *create(uint16_t log_version,
            const MetaLog::EntityHeader &header) {
      if (header.type == SchemaUpdateEntity::CREATE_TABLE)
        return new SchemaUpdateEntity::CreateTable(header);
      if (header.type == SchemaUpdateEntity::ALTER_TABLE)
        return new SchemaUpdateEntity::AlterTable(header);
      if (header.type == SchemaUpdateEntity::DROP_TABLE)
        return new SchemaUpdateEntity::DropTable(header);

      HT_ASSERT(!"shouldn't be here");
      return 0;
    }
};

} // namespace Hypertable
