/*
 * Memory BandWidth Partitioner.
 */

// ------------------------------------------------------------------------
INTERFACE [mbwp]:

#include "irq_chip.h"
#include "perf_cnt.h"
#include "per_cpu_data.h"

#include <cxx/dlist>

#include <cstdio>

class Cond_sc;
class Sched_context;

EXTENSION class Mbwp
{
public:
  static void handle_irq();
  static void update_stats();
  static void reset_counters();
  static Unsigned64 mbs_to_cache_events(Unsigned64);

  //static Per_cpu<Cond_sc *> sc;

private:
  // counters.
  enum {
    READ_CNT  = 0,
    WRITE_CNT = 1,
  };

  // cache events.
  enum {
    L2D_CACHE_REFILL = 0x17,
    L2D_CACHE_WB     = 0x18,
  };

  class Mbwp_irq : public Irq_base
  {
  public:
    Mbwp_irq()
    { set_hit(handler_wrapper<Mbwp_irq>); }

    void FIASCO_FLATTEN handle(Upstream_irq const *ui)
    {
      chip()->ack(pin());
      Upstream_irq::ack(ui);
      Mbwp::handle_irq();
    }

    ~Mbwp_irq() {};

  private:
    void switch_mode(bool) override {}
  };

  struct Mbwp_stats {
    Unsigned64 budget[2]; // 0 = read, 1 = write
    Unsigned64 periods = 0;
  };

  static void setup_counter(unsigned, unsigned);
  static void reset_counter(unsigned);
  static void setup_irq(unsigned);
  static void init_platform();
  static void init_config();

  //typedef cxx::Sd_list<Sched_context> Throttle_q;

  static Per_cpu<Mbwp_irq> _irq;
  static Per_cpu<Mbwp_stats> _stats;
  //static Per_cpu<Throttle_q> _throttled;
};

// ------------------------------------------------------------------------
IMPLEMENTATION [mbwp]:

#include "irq_mgr.h"
#include "sched_context.h"
#include "context.h"
#include "ready_queue.h"
#include "sched_constraint.h"
#include "ram_quota.h"

DEFINE_PER_CPU Per_cpu<Mbwp::Mbwp_irq> Mbwp::_irq;
DEFINE_PER_CPU Per_cpu<Mbwp::Mbwp_stats> Mbwp::_stats;
//DEFINE_PER_CPU Per_cpu<Cond_sc *> Mbwp::sc;

IMPLEMENT_OVERRIDE static
void
Mbwp::init()
{
  printf("MBWP INIT\n");
  // Initialize arch-specific counters.
  setup_counter(READ_CNT, L2D_CACHE_REFILL);
  setup_counter(WRITE_CNT, L2D_CACHE_WB);

  // Initialize platform-specific IRQs.
  init_platform();

  // Initialize configuration.
  init_config();

  reset_counter(READ_CNT);
  reset_counter(WRITE_CNT);

  //Cond_sc *_sc = Cond_sc::create(Ram_quota::root);
  //_sc->set_run(true);
  //sc.current() = _sc;
}

IMPLEMENT static
void
Mbwp::setup_counter(unsigned counter, unsigned event)
{
  Perf_cnt::setup_pmc(counter, event, 0xC0DE, 0xC0DE, 0xC0DE);
  Perf_cnt::enable_oflow_irq(counter);
}

IMPLEMENT static
void
Mbwp::reset_counters()
{
  reset_counter(READ_CNT);
  reset_counter(WRITE_CNT);
}

IMPLEMENT static
void
Mbwp::reset_counter(unsigned counter)
{
  //Mword val = 0UL - _stats.current().budget[counter];
  Mword val = 0;
  Mbw_sc *sc = Ready_queue::rq.current().mbw_sc();
  if (sc)
    val = sc->get_budget(counter);
  Perf_cnt::write_counter(counter, val);
}

IMPLEMENT static
void
Mbwp::setup_irq(unsigned irq_nr)
{
  Irq_mgr::mgr->alloc(&(_irq.current()), irq_nr);
  _irq.current().chip()->set_cpu(irq_nr, current_cpu());
  _irq.current().unmask();
}

IMPLEMENT static
void
Mbwp::handle_irq()
{
  //sc.current()->set_run(false);
  Mbw_sc *sc = Ready_queue::rq.current().mbw_sc();
  if (sc)
  {
    sc->set_run(false);
    sc->set_timeout();
  }

  // TODO: maybe ack in one write?
  Perf_cnt::ack_oflow_irq(READ_CNT);
  Perf_cnt::ack_oflow_irq(WRITE_CNT);

  current()->schedule();
}

IMPLEMENT_OVERRIDE static
void
Mbwp::handle_period()
{
  update_stats();

  reset_counter(READ_CNT);
  reset_counter(WRITE_CNT);

  //sc.current()->release();
}

IMPLEMENT static
void
Mbwp::update_stats()
{
  Mbwp_stats stats = _stats.current();
  stats.periods++;
}

IMPLEMENT static
Unsigned64
Mbwp::mbs_to_cache_events(Unsigned64 mbs)
{
  //static_assert(!Config::Scheduler_one_shot);
  //unsigned cache_line = Mmu<0, false>::dcache_line_size();
  //return (((mbs * 1000 * 1000) * Config::Scheduler_granularity) / (1000000 * cache_line));
  return ((mbs * 1000) / 64);
}

IMPLEMENT_DEFAULT static
void
Mbwp::init_platform()
{}

IMPLEMENT_DEFAULT static
void
Mbwp::init_config()
{}

