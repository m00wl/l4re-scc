// -----------------------------------------------------------
INTERFACE [iommu && !arm_iommu_stage2]:

/**
 * When the SMMU is configured to use stage 1 translation, we use the kernel
 * page table layout for the SMMU. For a non-virtualization-enabled kernel, this
 * is equivalent to the regular memory space page table layout. However, for a
 * virtualization-enabled kernel, memory spaces use a stage 2 page table layout,
 * whereas the kernel uses a stage 1 page table layout.
 */
EXTENSION class Dmar_space
{
  class Pte_ptr :
    public Pte_long_desc<Pte_ptr>,
    public Pte_iommu<Pte_ptr>,
    public Pte_long_attribs<Pte_ptr, Kernel_page_attr>,
    public Pte_generic<Pte_ptr, Unsigned64>
  {
  public:
    enum
    {
      Super_level = ::K_pte_ptr::Super_level,
      Max_level   = ::K_pte_ptr::Max_level,
    };
    Pte_ptr() = default;
    Pte_ptr(void *p, unsigned char level) : Pte_long_desc<Pte_ptr>(p, level) {}

    unsigned char page_order() const
    {
      return Ptab::Level<Dmar_ptab_traits_vpn>::shift(level)
        + Dmar_ptab_traits_vpn::Head::Base_shift;
    }
  };

  using Dmar_ptab_traits_vpn = K_ptab_traits_vpn;
};

// -----------------------------------------------------------
INTERFACE [iommu && arm_iommu_stage2 && cpu_virt]:

/**
 * When the SMMU is configured to use stage 2 translation, we use the regular
 * stage 2 page table layout also for the SMMU.
 */
EXTENSION class Dmar_space
{
public:
  static inline constexpr unsigned start_level()
  {
    // For non-ARM_PT48 the first page table is concatenated (10-bits), we skip
    // level zero.
    return (Page::Vtcr_bits >> 6) & 0x11b;
  }

private:
  class Pte_ptr :
    public Pte_long_desc<Pte_ptr>,
    public Pte_iommu<Pte_ptr>,
    public Pte_stage2_attribs<Pte_ptr, Page>,
    public Pte_generic<Pte_ptr, Unsigned64>
  {
  public:
    enum
    {
      Super_level = ::Pte_ptr::Super_level,
      Max_level   = ::Pte_ptr::Max_level,
    };
    Pte_ptr() = default;
    Pte_ptr(void *p, unsigned char level) : Pte_long_desc<Pte_ptr>(p, level) {}

    unsigned char page_order() const
    {
      return Ptab::Level<Dmar_ptab_traits_vpn>::shift(level)
        + Dmar_ptab_traits_vpn::Head::Base_shift;
    }
  };

  using Dmar_ptab_traits_vpn = Ptab_traits_vpn;
};

// -----------------------------------------------------------
INTERFACE [iommu]:

EXTENSION class Dmar_space
{
  using Dmar_pdir = Pdir_t<Pte_ptr, Dmar_ptab_traits_vpn, Ptab_va_vpn>;
  Dmar_pdir *_dmarpt;

  using Dmarpt_alloc = Kmem_slab_t<Dmar_pdir, sizeof(Dmar_pdir)>;
  static Dmarpt_alloc _dmarpt_alloc;

  Iommu::Domain _domain{this};
};

// -----------------------------------------------------------
IMPLEMENTATION [iommu]:

#include "kmem.h"

PUBLIC static inline
unsigned
Dmar_space::virt_addr_size()
{
  return Dmar_pdir::page_order_for_level(0) + Dmar_pdir::Levels::size(0);
}

IMPLEMENT
void
Dmar_space::init_page_sizes()
{
  add_page_size(Mem_space::Page_order(12));
  add_page_size(Mem_space::Page_order(21)); // 2MB
  add_page_size(Mem_space::Page_order(30)); // 1GB
}

IMPLEMENT
void
Dmar_space::tlb_flush(bool)
{
  Iommu::tlb_invalidate_domain(_domain);
}

IMPLEMENT
int
Dmar_space::bind_mmu(Iommu *mmu, Unsigned32 stream_id)
{
  return mmu->bind(stream_id, _domain);
}

IMPLEMENT
int
Dmar_space::unbind_mmu(Iommu *mmu, Unsigned32 stream_id)
{
  return mmu->unbind(stream_id, _domain);
}

PRIVATE
void
Dmar_space::remove_from_all_iommus()
{
  for (auto &iommu : Iommu::iommus())
    iommu.remove(_domain);
}
