INTERFACE [noncont_mem]:

#include <config.h>

EXTENSION class Mem_layout
{
public:
  static Address phys_to_pmem(Address addr);
  static void add_pmem(Address phys, Address virt, unsigned long size);

private:
  static unsigned short __ph_to_pm[1UL << (32 - Config::SUPERPAGE_SHIFT)];
};


//------------------------------------------------------------------------
IMPLEMENTATION [noncont_mem]:

#include <config.h>
#include "panic.h"


PUBLIC static
Address
Mem_layout::pmem_to_phys(Address addr)
{
  panic("Mem_layout::pmem_to_phys(Address addr=%lx) is not implemented\n",
         addr);
  return 0;
}

PUBLIC static inline
Address
Mem_layout::pmem_to_phys(void const *addr)
{
  return pmem_to_phys(Address(addr));
}

unsigned short Mem_layout::__ph_to_pm[1 << (32 - Config::SUPERPAGE_SHIFT)];

IMPLEMENT inline NEEDS[<config.h>]
Address
Mem_layout::phys_to_pmem(Address phys)
{
  Address virt = ((unsigned long)__ph_to_pm[phys >> Config::SUPERPAGE_SHIFT])
    << 16;

  if (!virt)
    return ~0UL;

  return virt | (phys & (Config::SUPERPAGE_SIZE-1));
}



IMPLEMENT inline ALWAYS_INLINE NEEDS[<config.h>]
void
Mem_layout::add_pmem(Address phys, Address virt, unsigned long size)
{
  for (; size >= Config::SUPERPAGE_SIZE; size -= Config::SUPERPAGE_SIZE)
    {
      __ph_to_pm[phys >> Config::SUPERPAGE_SHIFT] = virt >> 16;
      phys += Config::SUPERPAGE_SIZE;
      virt += Config::SUPERPAGE_SIZE;
    }
}


//------------------------------------------------------------------------
IMPLEMENTATION [!noncont_mem]:

#include "panic.h"


PUBLIC static
Address
Mem_layout::pmem_to_phys(Address addr)
{
  panic("Mem_layout::pmem_to_phys(Address addr=%lx) is not implemented\n",
         addr);
  return 0;
}

PUBLIC static inline
Address
Mem_layout::pmem_to_phys(void const *addr)
{
  return pmem_to_phys(Address(addr));
}

PUBLIC static inline
Address
Mem_layout::phys_to_pmem(Address phys)
{ return phys - Sdram_phys_base + Map_base; }

