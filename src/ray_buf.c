#include <stdint.h>
#include <stddef.h>
#include <assert.h>

#include "ray_buf.h"

ray_buf_t* ray_buf_new(size_t size) {
  if (!size) size = 128;
  ray_buf_t* buf = (ray_buf_t*)malloc(sizeof(ray_buf_t));
  buf->base = (uint8_t*)malloc(size);
  buf->size = size;
  buf->head = buf->base;
  return buf;
}

void ray_buf_free(ray_buf_t* buf) {
  free(buf->base);
  free(buf);
}

void ray_buf_need(ray_buf_t* buf, size_t len) {
  size_t size = buf->size;
  if (!size) {
    size = 128;
    buf->base = (uint8_t*)malloc(size);
    buf->size = size;
    buf->head = buf->base;
  }
  ptrdiff_t head = buf->head - buf->base;
  ptrdiff_t need = head + len;
  while (size < need) size *= 2;
  if (size > buf->size) {
    buf->base = (uint8_t*)realloc(buf->base, size);
    buf->size = size;
    buf->head = buf->base + head;
  }
}
void ray_buf_init(ray_buf_t* buf, uint8_t* data, size_t len) {
  buf->base = NULL;
  buf->head = NULL;
  buf->size = 0;
  if (len > 0) {
    ray_buf_need(buf, len);
    if (data != NULL) {
      memcpy(buf->base, data, len);
      buf->head = buf->base + len;
    }
    else {
      buf->head = buf->base;
    }
  }
}
void ray_buf_put(ray_buf_t* buf, uint8_t val) {
  ray_buf_need(buf, 1);
  *(buf->head++) = val;
}
void ray_buf_put_uint16(ray_buf_t* buf, uint16_t val) {
  ray_buf_need(buf, 2);
  const uint8_t* p = (const uint8_t*)buf->head;
  *p++ = val;
  *p++ = val >> 8;
  buf->head = (uint8_t*)p;
}
void ray_buf_put_uint32(ray_buf_t* buf, uint32_t val) {
  ray_buf_need(buf, 4);
  const uint8_t* p = (const uint8_t*)buf->head;
  *p++ = val;
  *p++ = val >> 8;
  *p++ = val >> 16;
  *p++ = val >> 24;
  buf->head = (uint8_t*)p;
}
void ray_buf_put_uint64(ray_buf_t* buf, uint64_t val) {
  ray_buf_need(buf, 8);
  const uint8_t* p = (const uint8_t*)buf->head;
  *p++ = val;
  *p++ = val >> 8;
  *p++ = val >> 16;
  *p++ = val >> 24;
  *p++ = val >> 32;
  *p++ = val >> 40;
  *p++ = val >> 48;
  *p++ = val >> 56;
  buf->head = (uint8_t*)p;
}
void ray_buf_put_double(ray_buf_t* buf, double val) {
  uint64_t u64 = *(uint64_t*)(void*)&val;
  ray_buf_put_uint64(buf, u64);
}

void ray_buf_write(ray_buf_t* buf, uint8_t* data, size_t len) {
  ray_buf_need(buf, len);
  memcpy(buf->head, data, len);
  buf->head += len;
}

void ray_buf_write_uleb128(ray_buf_t* buf, uint32_t val) {
  ray_buf_need(buf, 5);
  size_t   n = 0;
  uint8_t* p = buf->head;
  for (; val >= 0x80; val >>= 7) {
    p[n++] = (uint8_t)((val & 0x7f) | 0x80);
  }
  p[n++] = (uint8_t)val;
  buf->head += n;
}

void ray_buf_set_offset(ray_buf_t* buf, ssize_t ofs) {
  if (ofs > buf->size) ofs = -1;
  if (ofs < 0) {
    buf->head = buf->base + buf->size + ofs;
  }
  else {
    buf->head = buf->base + ofs;
  }
}
size_t ray_buf_get_offset(ray_buf_t* buf) {
  return (size_t)(buf->head - buf->base);
}

uint8_t ray_buf_get(ray_buf_t* buf) {
  return *(buf->head++);
}
uint16_t ray_buf_get_uint16(ray_buf_t* buf) {
  const uint8_t* p = (const uint8_t)buf->head;
  uint16_t v = *p++;
  v += (*p++) << 8;
  buf->head = (uint8_t*)p;
  return v;
}
uint32_t ray_buf_get_uint32(ray_buf_t* buf) {
  const uint8_t* p = (const uint8_t)buf->head;
  uint32_t v = *p++;
  v += (*p++) << 8;
  v += (*p++) << 16;
  v += (*p++) << 24;
  buf->head = (uint8_t*)p;
  return v;

}
uint64_t ray_buf_get_uint64(ray_buf_t* buf) {
  const uint8_t* p = (const uint8_t)buf->head;
  uint64_t v = *p++;
  v += (*p++) << 8;
  v += (*p++) << 16;
  v += (*p++) << 24;
  v += (*p++) << 32;
  v += (*p++) << 40;
  v += (*p++) << 48;
  v += (*p++) << 56;
  buf->head = (uint8_t*)p;
  return v;
}
uint32_t ray_buf_get_uleb128(ray_buf_t* buf) {
  const uint8_t* p = (const uint8_t*)buf->head;
  uint32_t v = *p++;
  if (v >= 0x80) {
    int sh = 0;
    v &= 0x7f;
    do {
     v |= ((*p & 0x7f) << (sh += 7));
    } while (*p++ >= 0x80);
  }
  buf->head = (uint8_t*)p;
  return v;
}
double ray_buf_get_double(ray_buf_t* buf) {
  uint64_t u64 = ray_buf_get_uint64(buf);
  return *(double*)(void*)&u64;
}

uint8_t ray_buf_peek(ray_buf_t* buf) {
  return *buf->head;
}
uint8_t* ray_buf_read(ray_buf_t* buf, size_t len) {
  assert(buf_get_offset(buf) + len <= buf->size);
  uint8_t* p = buf->head;
  buf->head += len;
  return p;
}
void ray_buf_clear(ray_buf_t* buf) {
  memset((void*)buf->base, 0, buf->size);
  buf->head = buf->base;
}


