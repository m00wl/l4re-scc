IMPLEMENTATION [arm && pf_layerscape]:

#include "infinite_loop.h"
#include "io.h"
#include "kmem.h"
#include "mmio_register_block.h"

void __attribute__ ((noreturn))
platform_reset(void)
{
  Mmio_register_block r(Kmem::mmio_remap(0x02ad0000, sizeof(Unsigned16)));
  r.r<16>(0x0) = 1 << 2;

  L4::infinite_loop();
}
