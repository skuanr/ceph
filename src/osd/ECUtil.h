// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2013 Inktank Storage, Inc.
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef ECUTIL_H
#define ECUTIL_H

#include <map>
#include <set>

#include "erasure-code/ErasureCodeInterface.h"
#include "include/buffer.h"
#include "include/assert.h"

namespace ECUtil {

const uint64_t CHUNK_ALIGNMENT = 64;
const uint64_t CHUNK_INFO = 8;
const uint64_t CHUNK_PADDING = 8;
const uint64_t CHUNK_OVERHEAD = 16; // INFO + PADDING

static uint64_t determine_chunk_size(
  uint64_t stripe_size,
  uint64_t stripe_width) {
  uint64_t unpadded = ((stripe_width / stripe_size) + CHUNK_OVERHEAD);
  if (unpadded % CHUNK_ALIGNMENT) {
    return unpadded - (unpadded % CHUNK_OVERHEAD) + CHUNK_OVERHEAD;
  } else {
    return unpadded;
  }
}


class stripe_info_t {
  const uint64_t stripe_size;
  const uint64_t stripe_width;
  const uint64_t chunk_size;
public:
  stripe_info_t(uint64_t stripe_size, uint64_t stripe_width)
    : stripe_size(stripe_size), stripe_width(stripe_width),
      chunk_size(determine_chunk_size(stripe_size, stripe_width)) {
    assert(stripe_width % stripe_size == 0);
  }
  uint64_t get_stripe_width() const {
    return stripe_width;
  }
  uint64_t get_chunk_size() const {
    return chunk_size;
  }
  uint64_t get_unpadded_chunk_size() const {
    return stripe_width / stripe_size;
  }
  uint64_t logical_to_prev_chunk_offset(uint64_t offset) const {
    return (offset / stripe_width) * chunk_size;
  }
  uint64_t logical_to_next_chunk_offset(uint64_t offset) const {
    return ((offset + stripe_width - 1)/ stripe_width) * chunk_size;
  }
  uint64_t logical_to_prev_stripe_offset(uint64_t offset) const {
    return offset - (offset % stripe_width);
  }
  uint64_t logical_to_next_stripe_offset(uint64_t offset) const {
    return offset % stripe_width ?
      offset - (offset % stripe_width) + stripe_width :
      offset;
  }
  uint64_t aligned_logical_offset_to_chunk_offset(uint64_t offset) const {
    assert(offset % stripe_width == 0);
    return (offset / stripe_width) * chunk_size;
  }
  uint64_t aligned_chunk_offset_to_logical_offset(uint64_t offset) const {
    assert(offset % chunk_size == 0);
    return (offset / chunk_size) * stripe_width;
  }
  pair<uint64_t, uint64_t> aligned_offset_len_to_chunk(
    pair<uint64_t, uint64_t> in) const {
    return make_pair(
      aligned_logical_offset_to_chunk_offset(in.first),
      aligned_logical_offset_to_chunk_offset(in.second));
  }
  pair<uint64_t, uint64_t> offset_len_to_stripe_bounds(
    pair<uint64_t, uint64_t> in) const {
    uint64_t off = logical_to_prev_stripe_offset(in.first);
    uint64_t len = logical_to_next_stripe_offset(
      (in.first - off) + in.second);
    return make_pair(off, len);
  }
};

void pack_append_chunk(
  const stripe_info_t &sinfo,
  uint32_t stripe_sum,
  bufferlist &raw_chunk,
  bufferlist *packed_chunk);

void unpack_chunk(
  const stripe_info_t &sinfo,
  bufferlist &packed_chunk,
  bufferlist *raw_chunk,
  uint32_t *stripe_sum = NULL,
  uint32_t *chunk_sum = NULL);

int unpack_verify_chunk(
  const stripe_info_t &sinfo,
  bufferlist &packed_chunk,
  bufferlist *raw_chunk,
  uint32_t *stripe_sum = NULL);

int decode(
  const stripe_info_t &sinfo,
  ErasureCodeInterfaceRef &ec_impl,
  map<int, bufferlist> &to_decode,
  bufferlist *out);

int decode(
  const stripe_info_t &sinfo,
  ErasureCodeInterfaceRef &ec_impl,
  map<int, bufferlist> &to_decode,
  map<int, bufferlist*> &out);

int encode(
  const stripe_info_t &sinfo,
  ErasureCodeInterfaceRef &ec_impl,
  bufferlist &in,
  const set<int> &want,
  map<int, bufferlist> *out);
};
#endif
