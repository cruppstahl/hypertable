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
#include "Common/Checksum.h"
#include "Common/Error.h"
#include "Common/Logger.h"

#include <boost/algorithm/string.hpp>

#include "CommitLogBlockStream.h"

using namespace Hypertable;

namespace {
  const uint32_t READAHEAD_BUFFER_SIZE = 131072;
}

bool CommitLogBlockStream::ms_assert_on_error = true;


CommitLogBlockStream::CommitLogBlockStream(FilesystemPtr &fs)
  : m_fs(fs), m_fd(-1), m_cur_offset(0), m_file_length(0),
    m_block_buffer(128), m_use_buffered_reader(true) {
}


CommitLogBlockStream::CommitLogBlockStream(FilesystemPtr &fs,
    const String &log_dir, const String &fragment, bool use_buffered_reader)
  : m_fs(fs), m_fd(-1), m_cur_offset(0), m_file_length(0),
    m_block_buffer(128), m_use_buffered_reader(use_buffered_reader) {
  load(log_dir, fragment);
}


CommitLogBlockStream::~CommitLogBlockStream() {
  close();
}


void CommitLogBlockStream::load(const String &log_dir, const String &fragment) {
  if (m_fd != -1)
    close();
  m_fragment = fragment;
  m_fname = log_dir + "/" + fragment;
  m_log_dir = log_dir;
  m_cur_offset = 0;
  m_file_length = m_fs->length(m_fname);
  if (m_use_buffered_reader)
    m_fd = m_fs->open_buffered(m_fname, Filesystem::OPEN_FLAG_DIRECTIO,
            READAHEAD_BUFFER_SIZE, 2);
  else
    m_fd = m_fs->open(m_fname, Filesystem::OPEN_FLAG_VERIFY_CHECKSUM);
}


void CommitLogBlockStream::close() {
  if (m_fd != -1) {
    try {
      m_fs->close(m_fd);
    }
    catch (Hypertable::Exception &e) {
      HT_ERRORF("Problem closing file %s - %s (%s)",
                m_fname.c_str(), e.what(), Error::get_text(e.code()));
    }
    m_fd = -1;
  }
}


bool
CommitLogBlockStream::next(CommitLogBlockInfo *infop,
                           BlockCompressionHeaderCommitLog *header,
                           bool refresh_length) {
  uint32_t nread;
  uint64_t offset = m_cur_offset;

  if (refresh_length) {
    try {
      m_file_length = m_fs->length(m_fname);
    }
    catch (Exception e) {
      if (boost::algorithm::ends_with(m_fname, ".purged"))
        HT_THROW(e.code(), e.what());
      HT_INFOF("Failed retrieving length for %s - trying %s.purged",
              m_fname.c_str(), m_fname.c_str());
      m_file_length = m_fs->length(m_fname + ".purged");
      m_fname += ".purged";
    }
  }

  assert(m_fd != -1);

  if (m_cur_offset >= m_file_length)
    return false;

  memset(infop, 0, sizeof(CommitLogBlockInfo));
  infop->log_dir = m_log_dir.c_str();
  infop->file_fragment = m_fragment.c_str();
  infop->start_offset = m_cur_offset;

  if ((infop->error = load_next_valid_header(header)) != Error::OK) {
    infop->end_offset = m_cur_offset;
    HT_INFO_OUT << "Seeking back to " << offset << HT_END;
    m_cur_offset = offset;
    m_fs->seek(m_fd, offset);
    return false;
  }

  m_cur_offset += header->length();

  // return unless data follows
  if (header->get_data_zlength() == 0) {
    infop->end_offset = m_cur_offset;
    infop->block_ptr = 0;
    infop->block_len = 0;
    return true;
  }

  m_block_buffer.ensure(header->length() + header->get_data_zlength());

  size_t toread = header->get_data_zlength();
  while ((nread = m_fs->read(m_fd, m_block_buffer.ptr, toread)) < toread) {
    if (nread <= 0) {
      reopen(m_cur_offset);
      continue;
    }
    toread -= nread;
    m_block_buffer.ptr += nread;
    m_cur_offset += nread;
  }
  m_block_buffer.ptr += nread;
  m_cur_offset += nread;

  if (nread != header->get_data_zlength()) {
    HT_WARNF("Commit log fragment '%s' truncated (entry start position %llu)",
             m_fname.c_str(), (Llu)(m_cur_offset - header->length()));
    m_fs->seek(m_fd, offset);
    m_cur_offset = offset;
    infop->end_offset = m_file_length;
    infop->error = Error::RANGESERVER_TRUNCATED_COMMIT_LOG;
    return true;
  }

  infop->end_offset = m_cur_offset;
  infop->block_ptr = m_block_buffer.base;
  infop->block_len = m_block_buffer.fill();

  return true;
}

void
CommitLogBlockStream::reopen(uint64_t offset) {
  HT_WARNF("Commit log fragment '%s' read returns -1, reopening",
          m_fname.c_str());
  m_fs->close(m_fd);
  m_fd = -1;

  sleep(5);

  try {
    m_fd = m_fs->open(m_fname, Filesystem::OPEN_FLAG_VERIFY_CHECKSUM);
  } 
  catch (Exception e) {
    if (e.code() != Error::DFSBROKER_FILE_NOT_FOUND)
      HT_THROW(e.code(), e.what());
    if (boost::algorithm::ends_with(m_fname, ".purged"))
      HT_THROW(e.code(), e.what());
    HT_INFOF("Failed opening %s - trying %s.purged",
            m_fname.c_str(), m_fname.c_str());
    m_fd = m_fs->open(m_fname + ".purged", Filesystem::OPEN_FLAG_VERIFY_CHECKSUM);
    m_fname += ".purged";
    m_file_length = m_fs->length(m_fname);
  }
  m_fs->seek(m_fd, offset);
}

int
CommitLogBlockStream::load_next_valid_header(
    BlockCompressionHeaderCommitLog *header) {
  try {
    size_t nread = 0;
    size_t toread = BlockCompressionHeader::LENGTH;
    uint64_t offset = m_cur_offset;

    m_block_buffer.ptr = m_block_buffer.base;
    m_block_buffer.ensure(toread);

    // read the block compression header; this will give us the full length
    // of the header
    while ((nread = m_fs->read(m_fd, m_block_buffer.ptr, toread)) < toread) {
      if (nread <= 0) {
        reopen(offset);
        continue;
      }
      toread -= nread;
      offset += nread;
      m_block_buffer.ptr += nread;
    }
    offset += nread;
    m_block_buffer.ptr += nread;

    size_t header_length = (size_t)m_block_buffer.base[10];

    // read the remaining bytes of the header
    if (header_length < BlockCompressionHeader::LENGTH) {
      HT_ERRORF("Bad commit log data: header length is only %d bytes, need at "
              "least %u bytes", (int)header_length,
              (int)BlockCompressionHeader::LENGTH);
      HT_THROW(Error::RANGESERVER_TRUNCATED_COMMIT_LOG, "Bad commit log data");
    }
    toread = header_length - BlockCompressionHeader::LENGTH;
    while ((nread = m_fs->read(m_fd, m_block_buffer.ptr, toread)) < toread) {
      if (nread <= 0) {
        reopen(offset);
        continue;
      }
      toread -= nread;
      offset += nread;
      m_block_buffer.ptr += nread;
    }

    // decode the full header
    m_block_buffer.ptr = m_block_buffer.base;
    header->decode((const uint8_t **)&m_block_buffer.ptr, &header_length);
  }
  catch (Exception &e) {
    HT_ERROR_OUT << m_fname << ": " << e << HT_END;
    if (ms_assert_on_error)
      HT_ABORT;
    return e.code();
  }
  return Error::OK;
}
