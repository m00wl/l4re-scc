
IMPLEMENTATION[ia32,amd64]:

#include "apic.h"
#include "config.h"
#include "cpu.h"
#include "io_apic.h"
#include "irq_mgr.h"
#include "koptions.h"
#include "mem_layout.h"
#include "pic.h"
#include "trap_state.h"
#include "watchdog.h"

IMPLEMENT inline NEEDS["mem_layout.h"]
void
Kernel_thread::free_initcall_section()
{
  // just fill up with invalid opcodes
  for (char *i = const_cast<char *>(Mem_layout::initcall_start);
       i + 1 < const_cast<char *>(Mem_layout::initcall_end); i += 2)
    {
      // UD2
      i[0] = 0x0f;
      i[1] = 0x0b;
    }
}


IMPLEMENT FIASCO_INIT
void
Kernel_thread::bootstrap_arch()
{
  // 
  // install our slow trap handler
  //
  nested_trap_handler      = Trap_state::base_handler;
  Trap_state::base_handler = thread_handle_trap;

  // initialize the profiling timer
  bool user_irq0 = Koptions::o()->opt(Koptions::F_irq0);

  if ((int)Config::Scheduler_mode == Config::SCHED_PIT && user_irq0)
    panic("option -irq0 not possible since irq 0 is used for scheduling");

  boot_app_cpus();
}

//--------------------------------------------------------------------------
IMPLEMENTATION [!mp]:

PUBLIC
static inline void
Kernel_thread::boot_app_cpus()
{}


//--------------------------------------------------------------------------
IMPLEMENTATION [mp]:

#include "acpi.h"
#include "per_cpu_data.h"

EXTENSION class Kernel_thread
{
public:
  static Cpu_number find_cpu_num_by_apic_id(Unsigned32 apic_id);
  static bool boot_deterministic;

private:
  typedef Per_cpu_array<Unsigned32> Apic_id_array;

  // store all APIC IDs found in the MADT
  // this is used by boot_ap_cpu() to determine its CPU number by looking up
  // its APIC ID in the array, the array position is equivalent to the CPU
  // number
  static Apic_id_array _cpu_num_to_apic_id;
};

Kernel_thread::Apic_id_array Kernel_thread::_cpu_num_to_apic_id;
bool Kernel_thread::boot_deterministic;

/**
 * Retrieve reserved CPU number for the given APIC ID.
 *
 * \param apic_id  Local APIC ID whose CPU number should be returned.
 *
 * \retval Cpu_number         The CPU number reserved for the provided APIC ID.
 * \retval Cpu_number::nil()  No valid CPU number for the provided APIC ID was
 *                            found.
 */
IMPLEMENT
Cpu_number
Kernel_thread::find_cpu_num_by_apic_id(Unsigned32 apic_id)
{
  for (Cpu_number n = Cpu_number::first(); n < Config::max_num_cpus(); ++n)
    if (_cpu_num_to_apic_id[n] == apic_id)
      return n;

  return Cpu_number::nil();
}

PRIVATE
static bool
Kernel_thread::boot_app_cpus_in_order(Address tramp_page)
{
  Acpi_madt const *madt = Io_apic::madt();
  if (!madt)
    return false;

  enum { Map_size = 512 }; // Config::Max_num_cpus is not enough as Map_size is the maximum APIC-ID
  Bitmap<Map_size> map;
  map.clear_all();

  unsigned n, cpu_cnt = 0;
  printf("Checking MADT-APIC\n");
  for (n = 0; n < Map_size; ++n)
    {
      auto lapic = madt->find<Acpi_madt::Lapic>(n);
      if (!lapic)
        break;

      printf("  cpu=%x apic=%x flags=%x (enabled: %d)%s\n",
             lapic->apic_processor_id, lapic->apic_id,
             lapic->flags, lapic->flags & 1,
             lapic->apic_id == Apic::apic.current()->cpu_id()
               ? " [Current CPU]" : "");

      if (lapic->apic_id == Apic::apic.current()->cpu_id())
        continue;

      if ((lapic->flags & 1) == 0) // Disabled?
        continue;

      map.set_bit(lapic->apic_id);
      ++cpu_cnt;
    }
  printf("Detected %d CPUs so far\n", cpu_cnt);
  printf("Checking MADT-X2APIC\n");
  for (n = 0; n + n < Map_size; ++n)
    {
      auto lx2apic = madt->find<Acpi_madt::Local_x2apic>(n);
      if (!lx2apic)
        break;

      printf("  cpu=%x apic=%x flags=%x (enabled: %d)%s\n",
             lx2apic->uid, lx2apic->local_apic_id,
             lx2apic->lapic_flags, lx2apic->lapic_flags & 1,
             lx2apic->local_apic_id == Apic::apic.current()->cpu_id()
               ? " [Current CPU]" : "");

      if (lx2apic->local_apic_id == Apic::apic.current()->cpu_id())
        continue;

      if ((lx2apic->lapic_flags & 1) == 0) // Disabled?
        continue;

      map.set_bit(lx2apic->local_apic_id);
      ++cpu_cnt;
    }
  printf("Detected %d CPUs\n", cpu_cnt);

  Unsigned64 start = Cpu::rdtsc();
  unsigned c = 1;
  unsigned num_cpus = 1;
  Cpu const *cpu = Cpu::boot_cpu();
  for (n = 0; n < Map_size; ++n)
    if (map[n])
      {
        int e = Apic::mp_startup(cpu, n, tramp_page);
        if (e)
          printf("MP Bootup: Some failure: %d\n", e);

        if (0) printf("waiting for %d to come up...\n", n);
        Unsigned64 wait_till = cpu->time_us() + 500000;
        while (!Cpu::online(Cpu_number(c)) && cpu->time_us() < wait_till)
          {
            Mem::barrier();
            Proc::pause();
          }

        if (1)
          printf("CPU%d/APIC%x is o%sline\n", c, n,
                 Cpu::online(Cpu_number(c)) ? "n" : "ff");
        if (Cpu::online(Cpu_number(c)))
          ++num_cpus;
        ++c;
      }
  Unsigned64 end = Cpu::rdtsc();

  printf("Done kicking all CPUs, %d CPUs online, took overall %lld cycles.\n",
         num_cpus, end - start);
  return true;
}

PUBLIC
static void
Kernel_thread::boot_app_cpus()
{
  // where to start the APs for detection of the APIC-IDs
  extern char _tramp_mp_entry[];

  // feature enabling flags (esp. cache enabled flag and paging enabled flag)
  extern volatile Mword _tramp_mp_startup_cr0;

  // feature enabling flags (esp. needed for big pages)
  extern volatile Mword _tramp_mp_startup_cr4;

  // physical address of the page table directory to use
  extern volatile Address _realmode_startup_pdbr;

  // pseudo descriptor for the gdt to load
  extern Pseudo_descriptor _tramp_mp_startup_gdt_pdesc;

  Address tramp_page;

  _realmode_startup_pdbr = Kmem::get_realmode_startup_pdbr();

  _tramp_mp_startup_cr4 = Cpu::get_cr4();
  _tramp_mp_startup_cr0 = Cpu::get_cr0();
  _tramp_mp_startup_gdt_pdesc
    = Pseudo_descriptor((Address)Cpu::boot_cpu()->get_gdt(), Gdt::gdt_max -1);

  __asm__ __volatile__ ("" : : : "memory");

  Acpi_madt const *madt = Io_apic::madt();

  // if we cannot find a MADT we cannot boot in deterministic order
  if (0 && madt)
    {
      boot_deterministic = true;
      Unsigned32 boot_apic_id = Apic::get_id();

      // make sure the boot CPU gets the right CPU number
      _cpu_num_to_apic_id[Cpu_number::boot_cpu()] = boot_apic_id;

      unsigned entry = 0;
      Cpu_number last_cpu = Cpu_number::first();

      // First we collect all enabled CPUs and assign them the leading CPU
      // numbers. Disabled CPUs are collected in a second run and get the
      // remaining CPU numbers assigned. This way we make sure that we can boot
      // at least the maximum number of enabled CPUs. Disabled CPUs may come
      // online later through e.g. hot plugging.
      while (last_cpu < Config::max_num_cpus())
        {
          auto const *lapic = madt->find<Acpi_madt::Lapic>(entry++);
          if (!lapic)
            break;

          // skip disabled CPUs
          if (!(lapic->flags & 1))
            continue;

          // skip logical boot CPU number
          if (last_cpu == Cpu_number::boot_cpu())
            ++last_cpu;

          Unsigned32 aid = ((Unsigned32)lapic->apic_id) << 24;

          // the boot CPU already has a CPU number assigned
          if (aid == boot_apic_id)
            continue;

          _cpu_num_to_apic_id[last_cpu++] = aid;
        }

      entry = 0;
      while (last_cpu < Config::max_num_cpus())
        {
          auto const *lapic = madt->find<Acpi_madt::Lapic>(entry++);
          if (!lapic)
            break;

          // skip enabled CPUs
          if (lapic->flags & 1)
            continue;

          // skip logical boot CPU number
          if (last_cpu == Cpu_number::boot_cpu())
            ++last_cpu;

          _cpu_num_to_apic_id[last_cpu++] = ((Unsigned32)lapic->apic_id) << 24;
        }
    }

  // Say what we do
  printf("MP: detecting APs...\n");

  // broadcast an AP startup via the APIC (let run the self-registration code)
  tramp_page = (Address)&(_tramp_mp_entry[0]);

  if (!boot_app_cpus_in_order(tramp_page))
    // Send IPI-Sequency to startup the APs
    Apic::mp_startup(Cpu::boot_cpu(), Apic::APIC_IPI_OTHERS, tramp_page);
}
