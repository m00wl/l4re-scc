IMPLEMENTATION [arm && pf_fvp_base]:

#include "infinite_loop.h"

void __attribute__ ((noreturn))
platform_reset(void)
{
  L4::infinite_loop();
}
