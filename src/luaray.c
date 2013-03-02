#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

#include "ray.h"

static ray_ctx_t* _RAY_CTX;

static int timer_new(lua_State* L) {
  ray_agent_t* self = (ray_agent_t*)lua_newuserdata(L, sizeof(ray_agent_t));
  ray_agent_init(self, _RAY_CTX);
  ray_timer_init(self);
  return 1;
}

static int lray_run(lua_State* L) {
  for (;;) {
    ray_evt_t* evt = ray_next(_RAY_CTX);
    int id = ray_get_id(evt->self);
    lua_getfield(L, LUA_REGISTRYINDEX, id);
    switch (evt->type) {
      case RAY_TIMER: {
        lua_getfield(L, -1, "on_timer");
        lua_call(L, 1, 0);
        break;
      }
      default {
        return luaL_error(L, "unhandled event type");
      }
    }
  }
}

static luaL_Reg lib_funcs[] = {
  {"run",       lray_run},
  {NULL,        NULL}
};

static luaL_Reg ctx_meths[] = {
  {"next",      ctx_next},
  {NULL,        NULL}
};

LUALIB_API int luaopen_ray(lua_State* L) {
  lua_settop(L, 0);

  _RAY_CTX = ray_ctx_new(1024);

  luaL_newmetatable(L, "ray.ctx");
  luaL_register(L, NULL, ctx_meths);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");
  lua_pop(L, 1);

  luaL_register(L, "ray", lib_funcs);
  return 1;
}

