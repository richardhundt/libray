#include "ray.h"

const char* ray_strerror(int code) {
  return uv_strerror((uv_errno_t)code);
}
const char* ray_err_name(int code) {
  return uv_err_name((uv_errno_t)code);
}

ray_evt_t ray_evt_init(ray_handle_t* h, ray_type_t t, int i, void* d) {
  ray_evt_t evt;
  evt.self = h;
  evt.type = t;
  evt.info = i;
  evt.data = d;
  TRACE("ray_evt_init - handle: %p, data: %p\n", a, d);
  return evt;
}

void ray_queue_async_cb(uv_async_t* async, int status) {
  (void)async;
  (void)status;
}
void ray_queue_timer_cb(uv_timer_t* timer, int status) {
  ray_queue_t* queue = container_of(timer, ray_queue_t, timer);
  ray_queue_interrupt(queue);
}

ray_queue_t* ray_queue_new(size_t size) {
  ray_queue_t* self = (ray_queue_t*)malloc(sizeof(ray_queue_t));
  ray_queue_init(self, size + (size % 2));
  return self;
}

int ray_queue_init(ray_queue_t* self, size_t size) {
  uv_loop_t* loop = uv_loop_new();

  self->loop = loop;
  loop->data = (void*)self;

  /* event pool */
  self->nput_evts = 0;
  self->nget_evts = 0;
  self->size_evts = size;
  self->evts = calloc(size, sizeof(ray_evt_t));

  /* message pool */
  self->nput_msgs = 0;
  self->nget_msgs = 0;
  self->size_msgs = size;
  self->msgs = calloc(size, sizeof(ray_msg_t));

  uv_async_init(loop, &self->async, ray_queue_async_cb);
  uv_unref((uv_handle_t*)&self->async);

  uv_timer_init(loop, &self->timer);
  uv_unref((uv_handle_t*)&self->timer);

  return 0;
}

void ray_queue_free(ray_queue_t* self) {
  free(self->evts);
  free(self->msgs);
  free(self);
}

ray_handle_t* ray_handle_new(ray_queue_t* queue) {
  ray_handle_t* self = (ray_handle_t*)calloc(1, sizeof(ray_handle_t));
  self->queue = queue;
  return self;
}
void ray_handle_free(ray_handle_t* self) {
  free(self);
}
int ray_evt_count(ray_queue_t* self) {
  if (self->nput_evts > self->nget_evts) {
    return self->nput_evts - self->nget_evts;
  }
  else { // integer wrap
    return self->nget_evts - self->nput_evts;
  }
}

ray_msg_t* ray_msg_next(ray_queue_t* self) {
  ray_msg_t* next_msg_p = &self->msgs[(self->nput_msgs++) % self->size_msgs];
  next_msg_p->queue = self;
  return next_msg_p;
}
void ray_msg_done(ray_msg_t* msg) {
  msg->queue->nget_msgs++;
}

void ray_queue_post(ray_queue_t* self, ray_evt_t* evt) {
  assert(ray_evt_count(self) != self->size_evts);
  if (ray_evt_count(self) == 0) {
    ray_queue_interrupt(self);
  }
  ray_evt_t* next_evt_p = &self->evts[(self->nput_evts + 1) % self->size_evts];
  assert(next_evt_p->data == NULL);
  self->evts[self->nput_evts++ % self->size_evts] = *evt;
}

ray_evt_t* ray_queue_take(ray_queue_t* self) {
  if (ray_evt_count(self) == 0) return NULL;
  return &self->evts[self->nget_evts++ % self->size_evts];
}
ray_evt_t* ray_queue_peek(ray_queue_t* self) {
  if (ray_evt_count(self) == 0) return NULL;
  return &self->evts[self->nget_evts % self->size_evts];
}
ray_evt_t* ray_queue_next(ray_queue_t* self) {
  int uv_again = 0;
  do {
    TRACE("try UV_RUN_NOWAIT\n");
    uv_again = uv_run(self->loop, UV_RUN_NOWAIT);
    if (ray_evt_count(self) != 0) break;

    TRACE("try UV_RUN_ONCE\n");
    uv_again = uv_run(self->loop, UV_RUN_ONCE);

    if (ray_evt_count(self) != 0) break;
  } while (uv_again);
  if (ray_evt_count(self) != 0) return ray_queue_take(self);
  return NULL;
}
void ray_evt_done(ray_evt_t* evt) {
  TRACE("ray_evt_done: evt: %p, data: %p\n", evt, evt->data);
  if (evt->data != NULL) free(evt->data);
  evt->data = NULL;
}

int ray_queue_interrupt(ray_queue_t* queue) {
  return uv_async_send(&queue->async);
}

/* ========================================================================== */
/* streams                                                                    */
/* ========================================================================== */
void ray_close_cb(uv_handle_t* handle) {
  ray_handle_t* self = container_of(handle, ray_handle_t, u);
  ray_evt_t evt = ray_evt_init(self, RAY_CLOSE, 0, NULL);
  ray_queue_post(self->queue, &evt);
}
void ray_close(ray_handle_t* self) {
  if (!uv_is_closing(&self->u.handle)) {
    uv_close(&self->u.handle, ray_close_cb);
  }
  else {
    //ray_evt_t evt = ray_evt_init(self, RAY_CLOSE, 0, NULL);
    //ray_queue_post(self->queue, &evt);
  }
}

uv_buf_t ray_alloc_cb(uv_handle_t* handle, size_t size) {
  return uv_buf_init(malloc(1024), 1024);
}

void ray_read_cb(uv_stream_t* stream, ssize_t nread, uv_buf_t buf) {
  TRACE("read_cb: nread %i\n", (int)nread);
  ray_handle_t* self = container_of(stream, ray_handle_t, u);
  if (nread == 0) {
    if (buf.base) free(buf.base);
    return;
  }

  ray_evt_t evt;

  if (nread > 0) {
    evt = ray_evt_init(self, RAY_READ, nread, buf.base);
  }
  else {
    uv_errno_t err = nread;
    evt = ray_evt_init(self, RAY_ERROR, err, NULL);
    TRACE("ERROR : %s\n", uv_strerror(err));
    if (buf.base) free(buf.base);
    uv_read_stop(stream);
    //ray_close(self);
  }

  ray_queue_post(self->queue, &evt);
}

void ray_write_cb(uv_write_t* req, int status) {
  ray_handle_t* self = (ray_handle_t*)req->data;
  ray_evt_t evt = ray_evt_init(self, RAY_WRITE, status, NULL);
  ray_queue_post(self->queue, &evt);
  ray_msg_t* msg = container_of(req, ray_msg_t, u);
  ray_msg_done(msg);
  ray_queue_interrupt(self->queue);
}

int ray_read_start(ray_handle_t* self) {
  TRACE("ray_read_start: %p\n", self);
  if (uv_is_closing(&self->u.handle)) {
    ray_evt_t evt = ray_evt_init(self, RAY_ERROR, UV__EIO, NULL);
    ray_queue_post(self->queue, &evt);
    ray_queue_interrupt(self->queue);
    return -1;
  }
  int rc = uv_read_start(&self->u.stream, ray_alloc_cb, ray_read_cb);
  TRACE("uv_read returned: %i\n", rc);
  return rc;
}
int ray_read_stop(ray_handle_t* self) {
  return uv_read_stop(&self->u.stream);
}

int ray_write(ray_handle_t* self, const char* str, size_t len) {
  uv_buf_t buf = uv_buf_init((char*)str, (unsigned int)len);
  ray_msg_t* msg = ray_msg_next(self->queue);
  msg->u.req.data = self;
  return uv_write(&msg->u.write, &self->u.stream, &buf, 1, ray_write_cb);
}

void ray_connection_cb(uv_stream_t* stream, int status) {
  ray_handle_t* self = container_of(stream, ray_handle_t, u);
  ray_evt_t evt = ray_evt_init(self, RAY_CONNECTION, status, NULL);
  TRACE("connection_cb on self %p\n", self);
  ray_queue_post(self->queue, &evt);
}

int ray_listen(ray_handle_t* self, int backlog) {
  return uv_listen(&self->u.stream, backlog, ray_connection_cb);
}

int ray_accept(ray_handle_t* server, ray_handle_t* client) {
  return uv_accept(&server->u.stream, &client->u.stream);
}

void* ray_handle_get_data(ray_handle_t* self) {
  return self->data;
}
void ray_handle_set_data(ray_handle_t* self, void* data) {
  self->data = data;
}

int ray_handle_get_id(ray_handle_t* self) {
  return self->id;
}
void ray_handle_set_id(ray_handle_t* self, int id) {
  self->id = id;
}

/* ========================================================================== */
/* timers                                                                     */
/* ========================================================================== */
void ray_timer_cb(uv_timer_t* timer, int status) {
  ray_handle_t* self = container_of(timer, ray_handle_t, u);
  ray_evt_t evt = ray_evt_init(self, RAY_TIMER, status, NULL);
  ray_queue_post(self->queue, &evt);
}

ray_handle_t* ray_timer_new(ray_queue_t* queue) {
  ray_handle_t* self = ray_handle_new(queue);
  if (uv_timer_init(queue->loop, &self->u.timer)) return NULL;
  return self;
}

int ray_timer_start(ray_handle_t* self, int64_t timeo, int64_t repeat) {
  return uv_timer_start(&self->u.timer, ray_timer_cb, timeo, repeat);
}
int ray_timer_stop(ray_handle_t* self) {
  return uv_timer_stop(&self->u.timer);
}

/* ========================================================================== */
/* TCP                                                                        */
/* ========================================================================== */
int ray_tcp_init(ray_handle_t* self) {
  return uv_tcp_init(self->queue->loop, &self->u.tcp);
}
ray_handle_t* ray_tcp_new(ray_queue_t* queue) {
  ray_handle_t* self = ray_handle_new(queue);
  if (ray_tcp_init(self)) return NULL;
  return self;
}

int ray_tcp_bind(ray_handle_t* self, const char* host, int port) {
  struct sockaddr_in addr;
  addr = uv_ip4_addr(host, port);
  return uv_tcp_bind(&self->u.tcp, addr);
}

/* ========================================================================== */
/* idle                                                                       */
/* ========================================================================== */
void ray_idle_cb(uv_idle_t* idle, int status) {
  ray_handle_t* self = container_of(idle, ray_handle_t, u);
  ray_evt_t evt = ray_evt_init(self, RAY_IDLE, status, NULL);
  ray_queue_post(self->queue, &evt);
}

ray_handle_t* ray_idle_new(ray_queue_t* queue) {
  ray_handle_t* self = ray_handle_new(queue);
  uv_idle_init(self->queue->loop, &self->u.idle);
  return self;
}
int ray_idle_start(ray_handle_t* self) {
  return uv_idle_start(&self->u.idle, ray_idle_cb);
}
int ray_idle_stop(ray_handle_t* self) {
  return uv_idle_stop(&self->u.idle);
}

/* ========================================================================== */
/* file system                                                                */
/* ========================================================================== */
void ray_stat_init(ray_stat_t* self, uv_stat_t* s) {
  if (s) {
    self->dev = s->st_dev;
    self->ino = s->st_ino;
    self->mode = s->st_mode;
    self->nlink = s->st_nlink;
    self->uid = s->st_uid;
    self->gid = s->st_gid;
    self->rdev = s->st_rdev;
    self->size = s->st_size;

    self->atim.tv_sec = s->st_atim.tv_sec;
    self->atim.tv_nsec = s->st_atim.tv_nsec;

    self->mtim.tv_sec = s->st_mtim.tv_sec;
    self->mtim.tv_nsec = s->st_mtim.tv_nsec;

    self->ctim.tv_sec = s->st_ctim.tv_sec;
    self->ctim.tv_nsec = s->st_ctim.tv_nsec;
  }
}

void ray_fs_cb(uv_fs_t* req) {
  ray_evt_t evt;
  if (req->result < 0) {
    evt = ray_evt_init(NULL, RAY_ERROR, req->result, NULL);
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
        ray_stat_init((ray_stat_t*)data, (uv_stat_t*)req->ptr);
        break;
      }
      case UV_FS_LSTAT: {
        type = RAY_FS_LSTAT;
        data = malloc(sizeof(ray_stat_t));
        ray_stat_init((ray_stat_t*)data, (uv_stat_t*)req->ptr);
        break;
      }
      case UV_FS_FSTAT: {
        type = RAY_FS_READDIR;
        data = malloc(sizeof(ray_stat_t));
        ray_stat_init((ray_stat_t*)data, (uv_stat_t*)req->ptr);
        break;
      }

      default: {
        TRACE("Unhandled fs_type");
        abort();
      }
    }
    evt = ray_evt_init(NULL, type, info, data);
  }

  uv_fs_req_cleanup(req);
  ray_msg_done(container_of(req, ray_msg_t, u));

  ray_queue_post((ray_queue_t*)req->loop->data, &evt);
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

int ray_fs_open(ray_queue_t* queue, const char *path, const char* how, int mode) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  int flags = ray_str_flags(how);
  return uv_fs_open(queue->loop, req, path, flags, mode, ray_fs_cb);
}

int ray_fs_read(ray_queue_t* queue, ray_file_t fh, char* buf, size_t len, int64_t ofs) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_read(queue->loop, req, fh, buf, len, ofs, ray_fs_cb); 
}

int ray_fs_unlink(ray_queue_t* queue, const char* path) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_unlink(queue->loop, req, path, ray_fs_cb);
}

int ray_fs_write(ray_queue_t* queue, ray_file_t file, void* buf, size_t len, int64_t ofs) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_write(queue->loop, req, file, buf, len, ofs, ray_fs_cb);
}

int ray_fs_mkdir(ray_queue_t* queue, const char* path, int mode) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_mkdir(queue->loop, req, path, mode, ray_fs_cb);
}

int ray_fs_rmdir(ray_queue_t* queue, const char* path) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_rmdir(queue->loop, req, path, ray_fs_cb);
}

int ray_fs_readdir(ray_queue_t* queue, const char* path) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_readdir(queue->loop, req, path, 0, ray_fs_cb);
}

int ray_fs_stat(ray_queue_t* queue, const char* path) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_stat(queue->loop, req, path, ray_fs_cb);
}

int ray_fs_rename(ray_queue_t* queue, const char* old_path, const char* new_path) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_rename(queue->loop, req, old_path, new_path, ray_fs_cb);
}

int ray_fs_sendfile(ray_queue_t* queue, ray_file_t ofh, ray_file_t ifh, int64_t ofs, size_t len) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_sendfile(queue->loop, req, ofh, ifh, ofs, len, ray_fs_cb);
}

int ray_fs_chmod(ray_queue_t* queue, const char* path, int mode) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_chmod(queue->loop, req, path, mode, ray_fs_cb);
}

int ray_fs_fchmod(ray_queue_t* queue, ray_file_t file, int mode) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_fchmod(queue->loop, req, file, mode, ray_fs_cb);
}

int ray_fs_utime(ray_queue_t* queue, const char* path, double atime, double mtime) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_utime(queue->loop, req, path, atime, mtime, ray_fs_cb);
}

int ray_fs_futime(ray_queue_t* queue, ray_file_t file, double atime, double mtime) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_futime(queue->loop, req, file, atime, mtime, ray_fs_cb);
}

int ray_fs_lstat(ray_queue_t* queue, const char* path) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_lstat(queue->loop, req, path, ray_fs_cb);
}

int ray_fs_link(ray_queue_t* queue, const char* path, const char* new_path) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_link(queue->loop, req, path, new_path, ray_fs_cb);
}

int ray_fs_symlink(ray_queue_t* queue, const char* p1, const char* p2, const char* f) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  int flags = ray_str_flags(f);
  return uv_fs_symlink(queue->loop, req, p1, p2, flags, ray_fs_cb);
}

int ray_fs_readlink(ray_queue_t* queue, const char* path) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_readlink(queue->loop, req, path, ray_fs_cb);
}

int ray_fs_chown(ray_queue_t* queue, const char* path, int uid, int gid) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_chown(queue->loop, req, path, uid, gid, ray_fs_cb);
}

int ray_fs_fchown(ray_queue_t* queue, ray_file_t file, int uid, int gid) {
  uv_fs_t* req = &(ray_msg_next(queue)->u.fs);
  return uv_fs_fchown(queue->loop, req, file, uid, gid, ray_fs_cb);
}

int ray_cwd(char* buffer, size_t len) {
  uv_errno_t err = uv_cwd(buffer, len);
  return err;
}

int ray_chdir(const char* dir) {
  uv_errno_t err = uv_chdir(dir);
  return err;
}

int ray_exepath(char* buffer, size_t* size) {
  return uv_exepath(buffer, size);
}

