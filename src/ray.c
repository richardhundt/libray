#include "ray.h"

int ray_last_error(ray_ctx_t* self) {
  uv_err_t err = uv_last_error(self->loop);
  return err.code;
}
const char* ray_strerror(int code) {
  uv_err_t err = { .code = code };
  return uv_strerror(err);
}
const char* ray_err_name(int code) {
  uv_err_t err = { .code = code };
  return uv_err_name(err);
}

void ray_buf_need(ray_buf_t* buf, size_t len) {
  size_t size = buf->size;
  size_t need = buf->offs + len;
  if (size == 0) {
    ray_buf_init(buf);
    size = buf->size;
  }
  if (need > buf->size) {
    while (size < need) size *= 2;
    buf->base = (char*)realloc(buf->base, size);
    buf->size = size;
  }
  printf("buf->base: %p\n", buf->base);
}
void ray_buf_init(ray_buf_t* buf) {
  buf->base = calloc(1, RAY_BUF_SIZE);
  buf->size = RAY_BUF_SIZE;
  buf->offs = 0;
}
void ray_buf_write(ray_buf_t* buf, const char* data, size_t len) {
  ray_buf_need(buf, len);
  memcpy(buf->base + buf->offs, data, len);
  buf->offs += len;
}
const char* ray_buf_read(ray_buf_t* buf) {
  buf->offs = 0;
  return buf->base;
}
void ray_buf_clear(ray_buf_t* buf) {
  memset(buf->base, 0, buf->size);
  buf->offs = 0;
}
size_t ray_buf_get_offset(ray_buf_t* buf) {
  return buf->offs;
}

ray_evt_t ray_evt_init(ray_agent_t* a, ray_type_t t, int i, void* d) {
  ray_evt_t evt;
  evt.self = a;
  evt.type = t;
  evt.info = i;
  evt.data = d;
  printf("ray_evt_init - agent: %p, data: %p\n", a, d);
  return evt;
}

void ray_ctx_async_cb(uv_async_t* async, int status) {
  (void)async;
  (void)status;
}
void ray_ctx_timer_cb(uv_timer_t* timer, int status) {
  ray_ctx_t* ctx = container_of(timer, ray_ctx_t, timer);
  ray_interrupt(ctx);
}

ray_ctx_t* ray_ctx_new(size_t size) {
  ray_ctx_t* self = (ray_ctx_t*)malloc(sizeof(ray_ctx_t));
  ray_ctx_init(self, size + (size % 2));
  return self;
}
int ray_ctx_init(ray_ctx_t* self, size_t size) {
  uv_loop_t* loop = uv_loop_new();
  self->nput = 0;
  self->nget = 0;
  self->size = size;
  self->loop = loop;
  loop->data = (void*)self;
  self->evts = calloc(size, sizeof(ray_evt_t));

  uv_async_init(loop, &self->async, ray_ctx_async_cb);
  uv_unref((uv_handle_t*)&self->async);

  uv_timer_init(loop, &self->timer);
  uv_unref((uv_handle_t*)&self->timer);

  self->sys = ray_agent_new(self);
  return 0;
}

void ray_ctx_free(ray_ctx_t* self) {
  ray_agent_free(self->sys);
  free(self->evts);
  free(self);
}

ray_agent_t* ray_agent_new(ray_ctx_t* ctx) {
  ray_agent_t* self = (ray_agent_t*)calloc(1, sizeof(ray_agent_t));
  self->ctx = ctx;
  return self;
}
void ray_agent_free(ray_agent_t* self) {
  printf("free agent: %p\n", self);
  free(self);
}
int ray_evt_count(ray_ctx_t* self) {
  if (self->nput > self->nget) {
    return self->nput - self->nget;
  }
  else { // integer wrap
    return self->nget - self->nput;
  }
}

void ray_post(ray_ctx_t* self, ray_evt_t* evt) {
  assert(ray_evt_count(self) != self->size);
  self->evts[self->nput++ % self->size] = *evt;
}

ray_evt_t* ray_take(ray_ctx_t* self) {
  if (ray_evt_count(self) == 0) return NULL;
  return &self->evts[self->nget++ % self->size];
}
ray_evt_t* ray_peek(ray_ctx_t* self) {
  if (ray_evt_count(self) == 0) return NULL;
  return &self->evts[self->nget % self->size];
}
ray_evt_t* ray_next(ray_ctx_t* self) {
  int uv_again = 0;
  do {
    printf("try UV_RUN_NOWAIT\n");
    uv_again = uv_run(self->loop, UV_RUN_NOWAIT);
    if (ray_evt_count(self) != 0) break;

    printf("try UV_RUN_ONCE\n");
    uv_again = uv_run(self->loop, UV_RUN_ONCE);

    printf("trigger\n");

    if (ray_evt_count(self) != 0) break;
  } while (uv_again);
  if (ray_evt_count(self) != 0) return ray_take(self);
  return NULL;
}
void ray_done(ray_evt_t* evt) {
  printf("ray_done: evt: %p, data: %p\n", evt, evt->data);
  if (evt->data) free(evt->data);
  evt->data = NULL;
  printf("OK\n");
}

int ray_interrupt(ray_ctx_t* ctx) {
  return uv_async_send(&ctx->async);
}

/* ========================================================================== */
/* streams                                                                    */
/* ========================================================================== */
void ray_close_cb(uv_handle_t* handle) {
  ray_agent_t* self = container_of(handle, ray_agent_t, h);
  ray_evt_t evt = ray_evt_init(self, RAY_CLOSE, 0, NULL);
  ray_post(self->ctx, &evt);
}
void ray_close(ray_agent_t* self) {
  if (!uv_is_closing(&self->h.handle)) {
    uv_close(&self->h.handle, ray_close_cb);
  }
  else {
    ray_evt_t evt = ray_evt_init(self, RAY_CLOSE, 0, NULL);
    ray_post(self->ctx, &evt);
  }
}

uv_buf_t ray_alloc_cb(uv_handle_t* handle, size_t size) {
  ray_agent_t* self = container_of(handle, ray_agent_t, h);
  ray_buf_need(&self->buf, size);
  return uv_buf_init(self->buf.base, size);
}

void ray_read_cb(uv_stream_t* stream, ssize_t nread, uv_buf_t buf) {
  printf("read_cb: nread %i\n", (int)nread);
  ray_agent_t* self = container_of(stream, ray_agent_t, h);
  if (nread == 0) return;

  ray_evt_t evt;

  if (nread > 0) {
    evt = ray_evt_init(self, RAY_READ, nread, strndup(self->buf.base, nread));
  }
  else {
    uv_err_t err = uv_last_error(stream->loop);
    evt = ray_evt_init(self, RAY_ERROR, err.code, NULL);
    ray_close(self);
  }

  ray_post(self->ctx, &evt);
}

void ray_write_cb(uv_write_t* req, int status) {
  printf("ray_write_cb\n");
  ray_agent_t* self = container_of(req, ray_agent_t, r);
  ray_evt_t evt = ray_evt_init(self, RAY_WRITE, status, NULL);
  ray_post(self->ctx, &evt);
  //ray_interrupt(self->ctx);
}

int ray_read_start(ray_agent_t* self, size_t len) {
  printf("ray_read_start: %p\n", self);
  if (!len) len = RAY_BUF_SIZE;
  printf("allocating buffer\n");
  ray_buf_need(&self->buf, len);
  printf("got buffer\n");
  int rc = uv_read_start(&self->h.stream, ray_alloc_cb, ray_read_cb);
  printf("uv_read_start returned: %i\n", rc);
  return rc;
}
int ray_read_stop(ray_agent_t* self) {
  return uv_read_stop(&self->h.stream);
}

int ray_write(ray_agent_t* self, const char* str, size_t len) {
  uv_buf_t buf = uv_buf_init((char*)str, (unsigned int)len);
  int rc = uv_write(&self->r.write, &self->h.stream, &buf, 1, ray_write_cb);
  printf("uv_write returned: %i\n", rc);
  return rc;
}

void ray_connection_cb(uv_stream_t* stream, int status) {
  ray_agent_t* self = container_of(stream, ray_agent_t, h);
  ray_evt_t evt = ray_evt_init(self, RAY_CONNECTION, status, NULL);
  printf("connection_cb on self %p\n", self);
  ray_post(self->ctx, &evt);
}

int ray_listen(ray_agent_t* self, int backlog) {
  return uv_listen(&self->h.stream, backlog, ray_connection_cb);
}

int ray_accept(ray_agent_t* server, ray_agent_t* client) {
  return uv_accept(&server->h.stream, &client->h.stream);
}

void* ray_get_data(ray_agent_t* self) {
  return self->data;
}
void ray_set_data(ray_agent_t* self, void* data) {
  self->data = data;
}

int ray_get_id(ray_agent_t* self) {
  return self->id;
}
void ray_set_id(ray_agent_t* self, int id) {
  self->id = id;
}

/* ========================================================================== */
/* timers                                                                     */
/* ========================================================================== */
void ray_timer_cb(uv_timer_t* timer, int status) {
  ray_agent_t* self = container_of(timer, ray_agent_t, h);
  ray_evt_t evt = ray_evt_init(self, RAY_TIMER, status, NULL);
  ray_post(self->ctx, &evt);
}

ray_agent_t* ray_timer_new(ray_ctx_t* ctx) {
  ray_agent_t* self = ray_agent_new(ctx);
  if (uv_timer_init(ctx->loop, &self->h.timer)) return NULL;
  return self;
}

int ray_timer_start(ray_agent_t* self, int64_t timeo, int64_t repeat) {
  return uv_timer_start(&self->h.timer, ray_timer_cb, timeo, repeat);
}
int ray_timer_stop(ray_agent_t* self) {
  return uv_timer_stop(&self->h.timer);
}

/* ========================================================================== */
/* TCP                                                                        */
/* ========================================================================== */
int ray_tcp_init(ray_agent_t* self) {
  ray_buf_init(&self->buf);
  return uv_tcp_init(self->ctx->loop, &self->h.tcp);
}
ray_agent_t* ray_tcp_new(ray_ctx_t* ctx) {
  ray_agent_t* self = ray_agent_new(ctx);
  if (ray_tcp_init(self)) return NULL;
  return self;
}

int ray_tcp_bind(ray_agent_t* self, const char* host, int port) {
  struct sockaddr_in addr;
  addr = uv_ip4_addr(host, port);
  return uv_tcp_bind(&self->h.tcp, addr);
}

/* ========================================================================== */
/* idle                                                                       */
/* ========================================================================== */
void ray_idle_cb(uv_idle_t* idle, int status) {
  ray_agent_t* self = container_of(idle, ray_agent_t, h);
  ray_evt_t evt = ray_evt_init(self, RAY_IDLE, status, NULL);
  ray_post(self->ctx, &evt);
}

ray_agent_t* ray_idle_new(ray_ctx_t* ctx) {
  ray_agent_t* self = ray_agent_new(ctx);
  uv_idle_init(self->ctx->loop, &self->h.idle);
  return self;
}
int ray_idle_start(ray_agent_t* self) {
  return uv_idle_start(&self->h.idle, ray_idle_cb);
}
int ray_idle_stop(ray_agent_t* self) {
  return uv_idle_stop(&self->h.idle);
}

/* ========================================================================== */
/* file system                                                                */
/* ========================================================================== */
void ray_stat_init(ray_stat_t* self, uv_statbuf_t* s) {
  if (s) {
    self->dev = s->st_dev;
    self->ino = s->st_ino;
    self->mode = s->st_mode;
    self->nlink = s->st_nlink;
    self->uid = s->st_uid;
    self->gid = s->st_gid;
    self->rdev = s->st_rdev;
    self->size = s->st_size;
    self->atime = s->st_atime;
    self->mtime = s->st_mtime;
    self->ctime = s->st_ctime;
  }
}

void ray_fs_cb(uv_fs_t* req) {
  ray_agent_t* sys = container_of(req, ray_agent_t, r);
  ray_evt_t evt;
  if (req->result == -1) {
    evt = ray_evt_init(sys, RAY_ERROR, req->errorno, NULL);
  }
  else {
    int   type = -1;
    void* data = NULL;
    int   info = 0;

    switch (req->fs_type) {
      case UV_FS_RENAME:
        type = RAY_FS_RENAME;
        info = req->result;
        break;
      case UV_FS_UNLINK:
        type = RAY_FS_UNLINK;
        info = req->result;
        break;
      case UV_FS_RMDIR:
        type = RAY_FS_RMDIR;
        info = req->result;
        break;
      case UV_FS_MKDIR:
        type = RAY_FS_MKDIR;
        info = req->result;
        break;
      case UV_FS_FSYNC:
        type = RAY_FS_FSYNC;
        info = req->result;
        break;
      case UV_FS_FTRUNCATE:
        type = RAY_FS_FTRUNCATE;
        info = req->result;
        break;
      case UV_FS_FDATASYNC:
        type = RAY_FS_FDATASYNC;
        info = req->result;
        break;
      case UV_FS_LINK:
        type = RAY_FS_LINK;
        info = req->result;
        break;
      case UV_FS_SYMLINK:
        type = RAY_FS_SYMLINK;
        info = req->result;
        break;
      case UV_FS_CHMOD:
        type = RAY_FS_CHMOD;
        info = req->result;
        break;
      case UV_FS_FCHMOD:
        type = RAY_FS_FCHMOD;
        info = req->result;
        break;
      case UV_FS_CHOWN:
        type = RAY_FS_CHOWN;
        info = req->result;
        break;
      case UV_FS_FCHOWN:
        type = RAY_FS_FCHOWN;
        info = req->result;
        break;
      case UV_FS_UTIME:
        type = RAY_FS_UTIME;
        info = req->result;
        break;
      case UV_FS_FUTIME:
        type = RAY_FS_FUTIME;
        info = req->result;
        break;
      case UV_FS_CLOSE:
        type = RAY_FS_CLOSE;
        info = req->result;
        break;
      case UV_FS_OPEN:
        type = RAY_FS_OPEN;
        info = req->result;
        break;
      case UV_FS_READ:
        type = RAY_FS_READ;
        data = req->data;
        info = req->result;
        break;
      case UV_FS_WRITE:
        type = RAY_FS_WRITE;
        info = req->result;
        break;
      case UV_FS_READLINK:
        type = RAY_FS_READLINK;
        info = strlen(req->ptr);
        data = strdup(req->ptr);
        break;
      case UV_FS_READDIR: {
        int i;
        ray_dir_t* dirs;
        type = RAY_FS_READDIR;
        info = req->result;
        data = calloc(info, sizeof(ray_dir_t));
        dirs = (ray_dir_t*)data;
        char* ptr = (char*)req->ptr;
        for (i = 0; i < info; i++) {
          char*  name = strdup(ptr);
          size_t nlen = strlen(ptr);
          dirs[i].name = name;
          dirs[i].nlen = nlen;
          ptr += nlen + 1;
        }
        break;
      }
      case UV_FS_STAT: {
        type = RAY_FS_STAT;
        data = malloc(sizeof(ray_stat_t));
        ray_stat_init((ray_stat_t*)data, (uv_statbuf_t*)req->ptr);
        break;
      }
      case UV_FS_LSTAT: {
        type = RAY_FS_LSTAT;
        data = malloc(sizeof(ray_stat_t));
        ray_stat_init((ray_stat_t*)data, (uv_statbuf_t*)req->ptr);
        break;
      }
      case UV_FS_FSTAT: {
        type = RAY_FS_READDIR;
        data = malloc(sizeof(ray_stat_t));
        ray_stat_init((ray_stat_t*)data, (uv_statbuf_t*)req->ptr);
        break;
      }

      default: {
        printf("Unhandled fs_type");
        abort();
      }
    }
    evt = ray_evt_init(sys, type, info, data);
  }

  uv_fs_req_cleanup(req);

  ray_post(sys->ctx, &evt);
}

int ray_str_flags(const char* str) {
  if (strcmp(str, "r") == 0)
    return O_RDONLY;

  if (strcmp(str, "r+") == 0)
    return O_RDWR;

  if (strcmp(str, "w") == 0)
    return O_CREAT | O_TRUNC | O_WRONLY;

  if (strcmp(str, "w+") == 0)
    return O_CREAT | O_TRUNC | O_RDWR;

  if (strcmp(str, "a") == 0)
    return O_APPEND | O_CREAT | O_WRONLY;

  if (strcmp(str, "a+") == 0)
    return O_APPEND | O_CREAT | O_RDWR;

  assert(0 && "Unknown file open flag");
}

int ray_fs_open(ray_ctx_t* ctx, const char *path, const char* how, int mode) {
  int flags = ray_str_flags(how);
  if (!mode) mode = 8;
  return uv_fs_open(ctx->loop, &ctx->sys->r.fs, path, flags, mode, ray_fs_cb);
}

int ray_fs_read(ray_ctx_t* ctx, ray_file_t fh, char* buf, size_t len, int64_t ofs) {
  return uv_fs_read(ctx->loop, &ctx->sys->r.fs, fh, buf, len, ofs, ray_fs_cb); 
}

