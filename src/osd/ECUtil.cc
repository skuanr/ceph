// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-

#include <errno.h>
#include "include/encoding.h"
#include "ECUtil.h"

void ECUtil::pack_append_chunk(
  const stripe_info_t &sinfo,
  uint32_t stripe_sum,
  bufferlist &raw_chunk,
  bufferlist *packed_chunk) {
  assert(packed_chunk);
  assert(packed_chunk->length() % sinfo.get_chunk_size() == 0);
  assert(raw_chunk.length() == sinfo.get_unpadded_chunk_size());
  uint32_t chunk_sum = raw_chunk.crc32c(0);
  packed_chunk->claim_append(raw_chunk);
  ::encode(stripe_sum, *packed_chunk);
  ::encode(chunk_sum, *packed_chunk);
  packed_chunk->append_zero(
    sinfo.get_chunk_size() - sinfo.get_unpadded_chunk_size() - CHUNK_INFO);
}

void ECUtil::unpack_chunk(
  const stripe_info_t &sinfo,
  bufferlist &packed_chunk,
  bufferlist *raw_chunk,
  uint32_t *stripe_sum,
  uint32_t *chunk_sum) {
  assert(raw_chunk);
  assert(packed_chunk.length() == sinfo.get_chunk_size());
  raw_chunk->substr_of(packed_chunk, 0, sinfo.get_unpadded_chunk_size());
  bufferlist::iterator p = packed_chunk.begin();
  p.seek(sinfo.get_unpadded_chunk_size());
  uint32_t sum;

  if (stripe_sum)
    ::decode(*stripe_sum, p);
  else
    ::decode(sum, p);

  if (chunk_sum)
    ::decode(*chunk_sum, p);
  else
    ::decode(sum, p);
}

int ECUtil::unpack_verify_chunk(
  const stripe_info_t &sinfo,
  bufferlist &packed_chunk,
  bufferlist *raw_chunk,
  uint32_t *stripe_sum) {
  uint32_t chunk_sum = 0;
  unpack_chunk(
    sinfo, packed_chunk, raw_chunk, stripe_sum, &chunk_sum);
  if (raw_chunk->crc32c(0) == chunk_sum)
    return 0;
  else
    return -EIO;
}

int ECUtil::decode(
  const stripe_info_t &sinfo,
  ErasureCodeInterfaceRef &ec_impl,
  map<int, bufferlist> &to_decode,
  bufferlist *out) {

  uint64_t obj_size = to_decode.begin()->second.length();

  assert(to_decode.size());
  assert(obj_size % sinfo.get_chunk_size() == 0);
  assert(out);
  assert(out->length() == 0);

  for (map<int, bufferlist>::iterator i = to_decode.begin();
       i != to_decode.end();
       ++i) {
    assert(i->second.length() == obj_size);
  }

  if (obj_size == 0)
    return 0;

  for (uint64_t i = 0; i < obj_size; i += sinfo.get_chunk_size()) {
    map<int, bufferlist> chunks;
    uint32_t first_stripe_sum = 0;
    for (map<int, bufferlist>::iterator j = to_decode.begin();
	 j != to_decode.end();
	 ++j) {
      uint32_t this_stripe_sum = 0;
      bufferlist bl;
      bl.substr_of(j->second, i, sinfo.get_chunk_size());
      int r = unpack_verify_chunk(
	sinfo,
	bl,
	&(chunks[j->first]),
	&this_stripe_sum);
      if (r < 0)
	return r;
      if (j == to_decode.begin())
	first_stripe_sum = this_stripe_sum;
      if (first_stripe_sum != this_stripe_sum)
	return -EINVAL;
    }
    bufferlist bl;
    int r = ec_impl->decode_concat(chunks, &bl);
    assert(bl.length() == sinfo.get_stripe_width());
    assert(r == 0);
    out->claim_append(bl);
  }
  return 0;
}

int ECUtil::decode(
  const stripe_info_t &sinfo,
  ErasureCodeInterfaceRef &ec_impl,
  map<int, bufferlist> &to_decode,
  map<int, bufferlist*> &out) {

  uint64_t obj_size = to_decode.begin()->second.length();

  assert(to_decode.size());
  assert(obj_size % sinfo.get_chunk_size() == 0);

  for (map<int, bufferlist>::iterator i = to_decode.begin();
       i != to_decode.end();
       ++i) {
    assert(i->second.length() == obj_size);
  }

  if (obj_size == 0)
    return 0;

  set<int> need;
  for (map<int, bufferlist*>::iterator i = out.begin();
       i != out.end();
       ++i) {
    assert(i->second);
    assert(i->second->length() == 0);
    need.insert(i->first);
  }

  for (uint64_t i = 0; i < obj_size; i += sinfo.get_chunk_size()) {
    map<int, bufferlist> chunks;
    uint32_t first_stripe_sum = 0;
    for (map<int, bufferlist>::iterator j = to_decode.begin();
	 j != to_decode.end();
	 ++j) {
      uint32_t this_stripe_sum = 0;
      bufferlist bl;
      bl.substr_of(j->second, i, sinfo.get_chunk_size());
      int r = unpack_verify_chunk(
	sinfo,
	bl,
	&(chunks[j->first]),
	&this_stripe_sum);
      assert(chunks[j->first].length() == sinfo.get_unpadded_chunk_size());
      if (r < 0)
	return r;
      if (j == to_decode.begin())
	first_stripe_sum = this_stripe_sum;
      if (first_stripe_sum != this_stripe_sum)
	return -EINVAL;
    }
    map<int, bufferlist> out_bls;
    int r = ec_impl->decode(need, chunks, &out_bls);
    assert(r == 0);
    for (map<int, bufferlist*>::iterator j = out.begin();
	 j != out.end();
	 ++j) {
      assert(out_bls.count(j->first));
      assert(out_bls[j->first].length() == sinfo.get_unpadded_chunk_size());
      pack_append_chunk(
	sinfo,
	first_stripe_sum,
	out_bls[j->first],
	j->second);
    }
  }
  for (map<int, bufferlist*>::iterator i = out.begin();
       i != out.end();
       ++i) {
    assert(i->second->length() == obj_size);
  }
  return 0;
}

int ECUtil::encode(
  const stripe_info_t &sinfo,
  ErasureCodeInterfaceRef &ec_impl,
  bufferlist &in,
  const set<int> &want,
  map<int, bufferlist> *out) {

  uint64_t logical_size = in.length();

  assert(logical_size % sinfo.get_stripe_width() == 0);
  assert(out);
  assert(out->empty());

  if (logical_size == 0)
    return 0;

  for (uint64_t i = 0; i < logical_size; i += sinfo.get_stripe_width()) {
    map<int, bufferlist> encoded;
    bufferlist buf;
    buf.substr_of(in, i, sinfo.get_stripe_width());
    uint32_t stripe_sum = buf.crc32c(0);
    int r = ec_impl->encode(want, buf, &encoded);
    assert(r == 0);
    for (map<int, bufferlist>::iterator i = encoded.begin();
	 i != encoded.end();
	 ++i) {
      assert(i->second.length() == sinfo.get_unpadded_chunk_size());
      pack_append_chunk(
	sinfo,
	stripe_sum,
	i->second,
	&((*out)[i->first]));
    }
  }

  for (map<int, bufferlist>::iterator i = out->begin();
       i != out->end();
       ++i) {
    assert(i->second.length() % sinfo.get_chunk_size() == 0);
    assert(
      sinfo.aligned_chunk_offset_to_logical_offset(i->second.length()) ==
      logical_size);
  }
  return 0;
}
