#ifndef _RAY_BUF_H_
#define _RAY_BUF_H_

#include "ray_common.h"

typedef struct ray_buf_t {
  size_t   size;
  uint8_t* head;
  uint8_t* base;
} ray_buf_t;

ray_buf_t* ray_buf_new(size_t size);

void ray_buf_need (ray_buf_t* buf, size_t len);
void ray_buf_init (ray_buf_t* buf, uint8_t* data, size_t len);
void ray_buf_free (ray_buf_t* buf);
void ray_buf_clear(ray_buf_t* buf);

void ray_buf_put (ray_buf_t* buf, uint8_t val);
void ray_buf_put_uint16 (ray_buf_t* buf, uint16_t val);
void ray_buf_put_uint32 (ray_buf_t* buf, uint32_t val);
void ray_buf_put_uint64 (ray_buf_t* buf, uint64_t val);
void ray_buf_put_double (ray_buf_t* buf, double val);
void ray_buf_put_uleb128 (ray_buf_t* buf, uint32_t val);

uint8_t  ray_buf_get (ray_buf_t* buf);
uint16_t ray_buf_get_uint16 (ray_buf_t* buf);
uint32_t ray_buf_get_uint32 (ray_buf_t* buf);
uint64_t ray_buf_get_uint64 (ray_buf_t* buf);
double   ray_buf_get_double (ray_buf_t* buf);
uint32_t ray_buf_get_uleb128 (ray_buf_t* buf);
uint8_t  ray_buf_peek (ray_buf_t* buf);

void ray_buf_write (ray_buf_t* buf, uint8_t* data, size_t len);
uint8_t* ray_buf_read (ray_buf_t* buf, size_t len);

size_t ray_buf_get_offset(ray_buf_t* buf);
void   ray_buf_set_offset(ray_buf_t* buf, ssize_t ofs);

#endif /* _RAY_BUF_H_ */
