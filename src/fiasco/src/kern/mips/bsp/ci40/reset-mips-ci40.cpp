IMPLEMENTATION [mips && ci40]:

#include "infinite_loop.h"
#include "kmem.h"
#include "mmio_register_block.h"

void __attribute__ ((noreturn))
platform_reset(void)
{
  Register_block<32> wdg(Kmem::mmio_remap(0x18102100, sizeof(Unsigned32)));
  wdg[0] = 1;

  L4::infinite_loop();
}
