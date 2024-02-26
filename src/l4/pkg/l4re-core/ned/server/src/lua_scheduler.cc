#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
#include "lua.h"

#include <l4/sys/scheduler>
#include <l4/sys/sched_constraint>

#include "lua_cap.h"

namespace Lua { namespace {

static int
__set_global_sc(lua_State *l)
{
  Lua::Cap *_s = check_cap(l, 1);
  Lua::Cap *_sc = check_cap(l, 2);

  auto s = _s->cap<L4::Scheduler>().get();
  auto sc = _sc->cap<L4::Sched_constraint>().get(); 
  l4_msgtag_t t = s->set_global_sc(sc);
  int r = l4_error(t);

  if (r < 0)
    luaL_error(l, "runtime error %s (%d)", l4sys_errtostr(r), r);

  return 0;
}

struct Scheduler_model
{
  static void
  register_methods(lua_State *l)
  {
  static const luaL_Reg l4_cap_class[] =
    {
      { "set_global_sc", __set_global_sc },
      { NULL, NULL }
    };
  luaL_setfuncs(l, l4_cap_class, 0);
  Cap::add_class_metatable(l);
  }
};


static Lua::Cap_type_lib<L4::Scheduler, Scheduler_model> __lib;

}}

