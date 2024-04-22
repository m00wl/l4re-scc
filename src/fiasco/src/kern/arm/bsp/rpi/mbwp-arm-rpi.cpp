IMPLEMENTATION [arm && mbwp && pf_rpi && pf_rpi_rpi4]:

IMPLEMENT_OVERRIDE static
void
Mbwp::init_platform()
{
  static constexpr auto SPI_OFFSET   = 32;
  static constexpr auto PMU_IRQ_BASE = 16;

  unsigned cpu_offs = cxx::int_value<Cpu_number>(current_cpu());
  unsigned global_irq = SPI_OFFSET + PMU_IRQ_BASE + cpu_offs;

  setup_irq(global_irq);
}

static constexpr unsigned config[4][2] = {
// { read, write } MB/s
   { 4000, 4000 },
   { 500, 500 },
   { 4000, 4000 },
   { 4000, 4000 },
};

PRIVATE static
void
Mbwp::set_budget(unsigned counter)
{
  unsigned cpu = cxx::int_value<Cpu_number>(current_cpu());
  auto budget = mbs_to_cache_events(config[cpu][counter]);
  _stats.current().budget[counter] = budget;
}

IMPLEMENT_OVERRIDE static
void
Mbwp::init_config()
{
  set_budget(READ_CNT);
  set_budget(WRITE_CNT);
}
