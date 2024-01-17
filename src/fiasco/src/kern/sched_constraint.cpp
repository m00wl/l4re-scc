/*
 * Scheduling constraints as first-class citizens.
 */

// --------------------------------------------------------------------------
INTERFACE [sched_fcc]:

#include <cxx/slist>
#include "member_offs.h"
#include "types.h"
#include "globals.h"
#include "kobject.h"
#include "ref_obj.h"

class Context;

#define SC_MAX_LIST_SIZE 1

class Sched_constraint
: public cxx::Dyn_castable<Sched_constraint, Kobject>,
  public Ref_cnt_obj
{
public:
  typedef Slab_cache Self_alloc;
  // TOMO: make blocked list a cxx list (just like semaphore):
  typedef cxx::S_list<Context> Blocked_list;
  // undefine default new operator.
  void *operator new(size_t);

  Ram_quota *get_quota() const
  { return _quota; }

  bool can_run() const
  { return _run; }

  void set_run(bool r)
  { _run = r; }

  Sched_constraint *get_next() const
  { return _next; }

  void set_next(Sched_constraint *sc)
  { _next = sc; }

  Context *get_blocked() const
  { return _blocked[0]; }

  void set_blocked(Context *c)
  { _blocked[0] = c; }

  virtual void deactivate() = 0;
  virtual void activate() = 0;
  virtual void migrate_away() = 0;
  virtual void migrate_to(Cpu_number) = 0;

  enum Type
  {
    Budget_sc,
    Time_window_sc,
  };

private:
  Ram_quota *_quota;
  Sched_constraint *_next;
  bool _run;
  Context *_blocked[SC_MAX_LIST_SIZE];
};

class Quant_sc : public Sched_constraint
{
public:
  Unsigned64 get_quantum() const
  { return _quantum; }

  void set_quantum(Unsigned64 q)
  { _quantum = q; }

  Unsigned64 get_left() const
  { return _left; }

  void set_left(Unsigned64 l)
  { _left = l; }

  void replenish()
  { set_left(_quantum); }

private:
  Unsigned64 _quantum;
  Unsigned64 _left;
};

class Budget_sc : public Sched_constraint
{
public:
  Unsigned64 get_budget() const
  { return _budget; }

  void set_budget(Unsigned64 b)
  { _budget = b; }

  Unsigned64 get_period() const
  { return _period; }

  void set_period(Unsigned64 p)
  { _period = p; }

  Unsigned64 get_left() const
  { return _left; }

  void set_left(Unsigned64 l)
  { _left = l; }

private:
  class Budget_sc_timeout : public Timeout
  {
  public:
    Budget_sc_timeout(Budget_sc *sc) : _sc(sc)
    {}

  protected:
    Budget_sc *_sc;
  };

  class Repl_timeout : public Budget_sc_timeout
  {
  public:
    Repl_timeout(Budget_sc *sc) : Budget_sc_timeout(sc)
    {}

  private:
    bool expired() override;
  };

  class Oob_timeout : public Budget_sc_timeout
  {
  public:
    Oob_timeout(Budget_sc *sc) : Budget_sc_timeout(sc)
    {}

  private:
    bool expired() override;
  };


  enum Operation
  {
    Op_Test,
    Op_Print,
  };

  Unsigned64 _budget;
  Unsigned64 _period;
  Unsigned64 _left;
  Oob_timeout _oob_timeout;
  Unsigned64 _next_repl;
  Repl_timeout _repl_timeout;
};

class Timewindow_sc : public Sched_constraint
{
private:
  Unsigned64 _min;
  Unsigned64 _max;
};

// --------------------------------------------------------------------------
IMPLEMENTATION [sched_fcc]:

#include <cassert>
#include <cstddef>
#include "cpu_lock.h"
#include "std_macros.h"
#include "config.h"
#include "lock_guard.h"
#include "thread_state.h"
#include "logdefs.h"
#include "types.h"
#include "processor.h"
#include "context.h"
#include "ready_queue.h"
#include "minmax.h"

PUBLIC inline NEEDS[<cstddef>]
void *
Sched_constraint::operator new (size_t, void *b) throw()
{ return b; }

PUBLIC
Sched_constraint::Sched_constraint(Ram_quota *q)
: _quota(q),
  _next(nullptr),
  _run(false)
{}

PUBLIC
Sched_constraint::~Sched_constraint()
{ printf("SC[%p]: delete\n", this); }

PUBLIC static inline
Mword
Sched_constraint::sched_classes()
{
  return 1UL << (-L4_sched_param_fixed_prio::Class);
}

PUBLIC
void
Sched_constraint::invoke(L4_obj_ref, L4_fpage::Rights, Syscall_frame *f, Utcb *)
                  override
{
  f->tag(commit_result(-L4_err::ENosys));
}

namespace {

static Kobject_iface * FIASCO_FLATTEN
sched_constraint_factory(Ram_quota *q, Space *, L4_msg_tag t, Utcb const *u,
                         int *err)
{
  if (t.words() != 3)
  {
    *err = L4_err::EInval;
    return nullptr;
  }

  *err = L4_err::ENomem;
  Sched_constraint *res;

  Sched_constraint::Type const *type;
  type = reinterpret_cast<Sched_constraint::Type const *>(&(u->values[2]));

  switch (*type)
  {
    case Sched_constraint::Type::Budget_sc:
      res = Budget_sc::create(q);
      break;
    default:
      *err = L4_err::EInval;
      res = nullptr;
      break;
  }

  // TOMO: increase ref count here or does it happen automatically?
  return res;
}

static inline void __attribute__((constructor)) FIASCO_INIT
register_factory()
{
  Kobject_iface::set_factory(L4_msg_tag::Label_sched_constraint,
                             sched_constraint_factory);
}

}

static Kmem_slab_t<Quant_sc> _quant_sc_allocator("Quant_sc");

PRIVATE static
Quant_sc::Self_alloc *
Quant_sc::allocator()
{ return _quant_sc_allocator.slab(); }

PUBLIC inline
void
Quant_sc::operator delete (void *ptr)
{
  Quant_sc *sc = reinterpret_cast<Quant_sc *>(ptr);
  allocator()->q_free<Ram_quota>(sc->get_quota(),  sc);
}

PUBLIC static
Quant_sc *
Quant_sc::create(Ram_quota *q)
{
  void *p = allocator()->q_alloc<Ram_quota>(q);
  return p ? new (p) Quant_sc(q) : 0;
}

PUBLIC
Quant_sc::Quant_sc(Ram_quota *q)
: Sched_constraint(q),
  _quantum(Config::Default_time_slice),
  _left(Config::Default_time_slice)
{}

PUBLIC
void
Quant_sc::deactivate() override
{ panic("Quant_sc::deactivate not implemented."); }

PUBLIC
void
Quant_sc::activate() override
{ panic("Quant_sc::activate not implemented."); }

PUBLIC
void
Quant_sc::migrate_away() override
{ panic("Quant_sc::migrate_away not implemented."); }

PUBLIC
void
Quant_sc::migrate_to(Cpu_number) override
{ panic("Quant_sc::migrate_to not implemented."); }

static Kmem_slab_t<Budget_sc> _budget_sc_allocator("Budget_sc");

PRIVATE static
Budget_sc::Self_alloc *
Budget_sc::allocator()
{ return _budget_sc_allocator.slab(); }

PUBLIC inline
void
Budget_sc::operator delete (void *ptr)
{
  Budget_sc *sc = reinterpret_cast<Budget_sc *>(ptr);
  allocator()->q_free<Ram_quota>(sc->get_quota(), sc);
}

PUBLIC static
Budget_sc *
Budget_sc::create(Ram_quota *q)
{
  void *p = allocator()->q_alloc<Ram_quota>(q);
  return p ? new (p) Budget_sc(q) : 0;
}

PUBLIC
Budget_sc::Budget_sc(Ram_quota *q)
: Sched_constraint(q),
  _budget(Config::Default_time_slice),
  _period(Config::Default_time_slice),
  _left(Config::Default_time_slice),
  _oob_timeout(this),
  _next_repl(0),
  _repl_timeout(this)
{ set_run(true); }

IMPLEMENT
bool
Budget_sc::Repl_timeout::expired()
{
  if (M_TIMER_DEBUG) printf("TIMER> BSC[%p]: replenishment timeout expired\n", _sc);
  return _sc->period_expired();
}

IMPLEMENT
bool
Budget_sc::Oob_timeout::expired()
{
  if (M_TIMER_DEBUG) printf("TIMER> BSC[%p]: timeslice timeout expired\n", _sc);
  _sc->timeslice_expired();
  // force reschedule
  return true;
}

PRIVATE
void
Budget_sc::calc_and_schedule_next_repl(Cpu_number cpu)
{
  Unsigned64 now = Timer::system_clock();
  if (now < _next_repl)
    return;

  Unsigned64 diff = now - _next_repl;
  unsigned n = diff / _period;

  _next_repl += (n + 1) * _period;

  if (_repl_timeout.is_set())
    _repl_timeout.reset();
  if (M_TIMER_DEBUG) printf("TIMER> BSC[%p]: setting replenishment timeout @ %llu\n", this, _next_repl);
  _repl_timeout.set(_next_repl, cpu);
}

PRIVATE
void
Budget_sc::replenish()
{
  set_left(_budget);
  set_run(true);
}

PRIVATE
void
Budget_sc::timeslice_expired()
{
  if (M_SCHEDULER_DEBUG) printf("SCHEDULER> BSC[%p]: timeslice_expired\n", this);
  set_left(0);
  set_run(false);
}

PRIVATE
bool
Budget_sc::period_expired()
{
  // TOMO: requeue here?
  if (M_SCHEDULER_DEBUG) printf("SCHEDULER> BSC[%p]: period_expired\n", this);
  replenish();
  calc_and_schedule_next_repl(current_cpu());

  Ready_queue &rq { Ready_queue::rq.current() };
  //Context *curr = rq.current();
  Context *curr = ::current();
  if (M_SCHEDULER_DEBUG) printf("SCHEDULER> RQ[%p]: current: C[%p]\n", &rq, curr);

  if ((curr != get_blocked())
      && get_blocked()
      && get_blocked()->state() & Thread_ready_mask)
    rq.ready_enqueue(get_blocked());

  if (curr && curr->sched_context_contains(this))
  {
    _oob_timeout.reset();
    activate();
  }

  //reschedule, if the replenished thread can preempt the current thread.
  return true;
}

PUBLIC
void
Budget_sc::deactivate() override
{
  if (M_SCHEDULER_DEBUG) printf("SCHEDULER> BSC[%p]: deactivate\n", this);
  Unsigned64 clock = Timer::system_clock();
  Signed64 left = _oob_timeout.get_timeout(clock);

  _left = max(left, static_cast<Signed64>(0));
  if (left < 0)
      printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>> BSC[%p]: budget overrun by %lld.\n", this, left * (-1));

  _oob_timeout.reset();
}

PUBLIC
void
Budget_sc::activate() override
{
  if (M_SCHEDULER_DEBUG) printf("SCHEDULER> BSC[%p]: activated on CPU %d\n", this, cxx::int_value<Cpu_number>(current_cpu()));
  Unsigned64 clock = Timer::system_clock();
  if (M_TIMER_DEBUG) printf("TIMER> BSC[%p]: setting timeslice timeout @ %llu\n", this, clock + _left);
  _oob_timeout.set(clock + _left, current_cpu());
}

PUBLIC
void
Budget_sc::migrate_away() override
{
  if (M_MIGRATION_DEBUG) printf("MIGRATION> BSC[%p]: migrate away\n", this);
  //deactivate();
  _repl_timeout.reset();

  assert(!_oob_timeout.is_set());
  assert(!_repl_timeout.is_set());
}

PUBLIC
void
Budget_sc::migrate_to(Cpu_number target) override
{
  if (M_MIGRATION_DEBUG) printf("MIGRATION> BSC[%p]: migrate to cpu %d\n", this, cxx::int_value<Cpu_number>(target));
  assert(!_oob_timeout.is_set());
  assert(!_repl_timeout.is_set());

  replenish();
  calc_and_schedule_next_repl(target);
}

PUBLIC
void
Budget_sc::invoke(L4_obj_ref self, L4_fpage::Rights rights, Syscall_frame *f,
                  Utcb *utcb) override
{
  (void)rights;

  L4_msg_tag res(L4_msg_tag::Schedule);

  if (EXPECT_TRUE(self.op() & L4_obj_ref::Ipc_send))
  {
    switch (utcb->values[0])
    {
      case Op_Test: res = test(); break;
      case Op_Print: res = print(); break;
      default:   res = commit_result(-L4_err::ENosys); break;
    }
  }

  f->tag(res);
}

PRIVATE
L4_msg_tag
Budget_sc::test()
{
  printf("SC[%p]: SYSCALL test!\n", this);
  return commit_result(0);
}

PRIVATE
L4_msg_tag
Budget_sc::print()
{
  printf("SC[%p]: SYSCALL print!\n", this);
  return commit_result(0);
}

