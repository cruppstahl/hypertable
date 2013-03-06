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

#ifndef HYPERTABLE_SCHEMAUPDATEENTITIES_H
#define HYPERTABLE_SCHEMAUPDATEENTITIES_H

#include "Hypertable/Lib/MetaLogEntity.h"

namespace Hypertable { namespace SchemaUpdateEntity {

enum {
  CREATE_TABLE = 0x30001,
  ALTER_TABLE  = 0x30002,
  DROP_TABLE   = 0x30003
};

class CreateTable : public MetaLog::Entity {
  public:
    CreateTable(const String &table_name, const String &table_id,
            const String &schema)
    : Entity(CREATE_TABLE), m_table_name(table_name), m_table_id(table_id),
      m_schema(schema) {
    }

    CreateTable(const MetaLog::EntityHeader &header_)
    : Entity(header_) {
    }

    virtual const String name() { return "CreateTable"; }

    virtual size_t encoded_length() const {
      return Serialization::encoded_length_vstr(m_table_name)
          + Serialization::encoded_length_vstr(m_table_id)
          + Serialization::encoded_length_vstr(m_schema);
    }

    virtual void encode(uint8_t **bufp) const {
      Serialization::encode_vstr(bufp, m_table_name);
      Serialization::encode_vstr(bufp, m_table_id);
      Serialization::encode_vstr(bufp, m_schema);
    }
    
    virtual void decode(const uint8_t **bufp, size_t *remainp) {
      m_table_name = Serialization::decode_vstr(bufp, remainp);
      m_table_id = Serialization::decode_vstr(bufp, remainp);
      m_schema = Serialization::decode_vstr(bufp, remainp);
    }

    virtual void display(std::ostream &os) {
      os << "SchemaUpdateEntity::" << name() << " table=" << m_table_name
          << " id=" << m_table_id << " schema=" << m_schema << " ";
    }

    String m_table_name;
    String m_table_id;
    String m_schema;
};

class AlterTable : public MetaLog::Entity {
  public:
    AlterTable(const String &table_name, const String &table_id,
            const String &schema)
    : Entity(ALTER_TABLE), m_table_name(table_name), m_table_id(table_id),
      m_schema(schema) {
    }

    AlterTable(const MetaLog::EntityHeader &header_)
    : Entity(header_) {
    }

    virtual const String name() { return "AlterTable"; }

    virtual size_t encoded_length() const {
      return Serialization::encoded_length_vstr(m_table_name)
          + Serialization::encoded_length_vstr(m_table_id)
          + Serialization::encoded_length_vstr(m_schema);
    }

    virtual void encode(uint8_t **bufp) const {
      Serialization::encode_vstr(bufp, m_table_name);
      Serialization::encode_vstr(bufp, m_table_id);
      Serialization::encode_vstr(bufp, m_schema);
    }
    
    virtual void decode(const uint8_t **bufp, size_t *remainp) {
      m_table_name = Serialization::decode_vstr(bufp, remainp);
      m_table_id = Serialization::decode_vstr(bufp, remainp);
      m_schema = Serialization::decode_vstr(bufp, remainp);
    }

    virtual void display(std::ostream &os) {
      os << "SchemaUpdateEntity::" << name() << " table=" << m_table_name
          << " id=" << m_table_id << " schema=" << m_schema << " ";
    }

    String m_table_name;
    String m_table_id;
    String m_schema;
};

class DropTable : public MetaLog::Entity {
  public:
    DropTable(const String &table_name, const String &table_id)
    : Entity(DROP_TABLE), m_table_name(table_name), m_table_id(table_id) {
    }

    DropTable(const MetaLog::EntityHeader &header_)
    : Entity(header_) {
    }

    virtual const String name() { return "DropTable"; }

    virtual size_t encoded_length() const {
      return Serialization::encoded_length_vstr(m_table_name)
          + Serialization::encoded_length_vstr(m_table_id);
    }

    virtual void encode(uint8_t **bufp) const {
      Serialization::encode_vstr(bufp, m_table_name);
      Serialization::encode_vstr(bufp, m_table_id);
    }
    
    virtual void decode(const uint8_t **bufp, size_t *remainp) {
      m_table_name = Serialization::decode_vstr(bufp, remainp);
      m_table_id = Serialization::decode_vstr(bufp, remainp);
    }

    virtual void display(std::ostream &os) {
      os << "SchemaUpdateEntity::" << name() << " table=" << m_table_name
          << " id=" << m_table_id << " ";
    }

    String m_table_name;
    String m_table_id;
};

} } // namespace Hypertable::SchemaUpdateEntity

#endif // HYPERTABLE_SCHEMAUPDATEENTITIES_H
