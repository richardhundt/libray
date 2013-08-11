#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifndef _WIN32
#include <unistd.h>
#endif

#ifdef WIN32
# define UNUSED /* empty */
# define INLINE __inline
#else
# define UNUSED __attribute__((unused))
# define INLINE inline
#endif

#undef RAY_DEBUG

#include "libuv/include/uv.h"

#ifdef RAY_DEBUG
#  define TRACE(fmt, ...) do { \
    fprintf(stderr, "%s: %d: %s: " fmt, \
    __FILE__, __LINE__, __func__, ##__VA_ARGS__); \
  } while (0)
#else
#  define TRACE(fmt, ...) ((void)0)
#endif /* RAY_DEBUG */


/* default buffer size for read operations */
#define RAY_BUF_SIZE 4096

/* max path length */
#define RAY_MAX_PATH 1024

#define container_of(ptr, type, member) \
  ((type*) ((char*)(ptr) - offsetof(type, member)))

typedef enum {
  RAY_UNKNOWN = -1,
  RAY_CUSTOM,
  RAY_ERROR,
  RAY_READ,
  RAY_WRITE,
  RAY_CLOSE,
  RAY_CONNECTION,
  RAY_TIMER,
  RAY_IDLE,
  RAY_CONNECT,
  RAY_SHUTDOWN,
  RAY_WORK,
  RAY_FS_CUSTOM,
  RAY_FS_ERROR,
  RAY_FS_OPEN,
  RAY_FS_CLOSE,
  RAY_FS_READ,
  RAY_FS_WRITE,
  RAY_FS_SENDFILE,
  RAY_FS_STAT,
  RAY_FS_LSTAT,
  RAY_FS_FSTAT,
  RAY_FS_FTRUNCATE,
  RAY_FS_UTIME,
  RAY_FS_FUTIME,
  RAY_FS_CHMOD,
  RAY_FS_FCHMOD,
  RAY_FS_FSYNC,
  RAY_FS_FDATASYNC,
  RAY_FS_UNLINK,
  RAY_FS_RMDIR,
  RAY_FS_MKDIR,
  RAY_FS_RENAME,
  RAY_FS_READDIR,
  RAY_FS_LINK,
  RAY_FS_SYMLINK,
  RAY_FS_READLINK,
  RAY_FS_CHOWN,
  RAY_FS_FCHOWN
} ray_type_t;

union ray_handle_u {
  uv_handle_t     handle;
  uv_stream_t     stream;
  uv_tcp_t        tcp;
  uv_pipe_t       pipe;
  uv_prepare_t    prepare;
  uv_check_t      check;
  uv_idle_t       idle;
  uv_async_t      async;
  uv_timer_t      timer;
  uv_fs_event_t   fs_event;
  uv_fs_poll_t    fs_poll;
  uv_poll_t       poll;
  uv_process_t    process;
  uv_tty_t        tty;
  uv_udp_t        udp;
};

union ray_msg_u {
  uv_req_t          req;
  uv_write_t        write;
  uv_connect_t      connect;
  uv_shutdown_t     shutdown;
  uv_fs_t           fs;
  uv_work_t         work;
  uv_udp_send_t     udp_send;
  uv_getaddrinfo_t  getaddrinfo;
};

typedef uv_file  ray_file_t;

typedef struct ray_evt_s   ray_evt_t;
typedef struct ray_msg_s   ray_msg_t;
typedef struct ray_req_s   ray_req_t;
typedef struct ray_queue_s ray_queue_t;
typedef struct ray_handle_s ray_handle_t;

typedef struct ray_timespec_s ray_timespec_t;

typedef struct ray_dir_s   ray_dir_t;
typedef struct ray_stat_s  ray_stat_t;
 
struct ray_evt_s {
  ray_type_t    type;
  ray_handle_t* self;
  int           info;
  void*         data;
};

struct ray_msg_s {
  union ray_msg_u u;
  ray_queue_t*    queue;
};

struct ray_queue_s {
  size_t        nput_evts;
  size_t        nget_evts;
  size_t        size_evts;
  ray_evt_t*    evts;

  size_t        nput_msgs;
  size_t        nget_msgs;
  size_t        size_msgs;
  ray_msg_t*    msgs;

  uv_loop_t*    loop;
  uv_async_t    async;
  uv_timer_t    timer;
};

struct ray_handle_s {
  union ray_handle_u u;
  ray_queue_t*       queue;
  int                id;
  void*              data;
};

struct ray_dir_s {
  char*  name;
  size_t nlen;
};

struct ray_timespec_s {
  long tv_sec;
  long tv_nsec;
};

struct ray_stat_s {
  uint32_t mode;
  uint32_t uid;
  uint32_t gid;
  uint64_t size;
  uint64_t dev;
  uint64_t rdev;
  uint64_t ino;
  uint64_t nlink;
  ray_timespec_t atim;
  ray_timespec_t mtim;
  ray_timespec_t ctim;
};

ray_queue_t* ray_queue_new(size_t size);
int ray_queue_init(ray_queue_t* self, size_t size);
void ray_queue_free(ray_queue_t* self);

int ray_evt_count(ray_queue_t* self);
ray_evt_t ray_evt_init(ray_handle_t* o, ray_type_t t, int i, void* d);

ray_handle_t* ray_handle_new(ray_queue_t* queue);
void ray_handle_free(ray_handle_t* self);

void ray_queue_post(ray_queue_t* self, ray_evt_t* evt);
ray_evt_t* ray_queue_take(ray_queue_t* self);
ray_evt_t* ray_queue_peek(ray_queue_t* self);
ray_evt_t* ray_queue_next(ray_queue_t* self);

int ray_last_error(ray_queue_t* self);
const char* ray_strerror(int code);
const char* ray_err_name(int code);

int ray_queue_interrupt(ray_queue_t* queue);

ray_handle_t* ray_tcp_new(ray_queue_t* queue);
int ray_tcp_init(ray_handle_t* self);
int ray_tcp_bind(ray_handle_t* self, const char* host, int port);

int ray_read_start(ray_handle_t* self);
int ray_read_stop(ray_handle_t* self);

int ray_write(ray_handle_t* self, const char* str, size_t len);

int ray_listen(ray_handle_t* self, int backlog);
int ray_accept(ray_handle_t* server, ray_handle_t* client);

void ray_close(ray_handle_t* self);

ray_handle_t* ray_timer_new(ray_queue_t* queue);
int ray_timer_start(ray_handle_t* self, int64_t timeo, int64_t repeat);
int ray_timer_stop(ray_handle_t* self);

ray_evt_t* ray_queue_next(ray_queue_t* self);
void ray_evt_done(ray_evt_t* evt);

int  ray_handle_get_id(ray_handle_t* self);
void ray_handle_set_id(ray_handle_t* self, int id);


