// --------------------------------------------------------------------------
INTERFACE:

#include "member_offs.h"
#include "types.h"
#include "globals.h"
#include "kobject.h"
#include "ref_obj.h"

#include "cxx/dlist"

class Sched_context;

class Sched_constraint
: public cxx::Dyn_castable<Sched_constraint, Kobject>,
  public Ref_cnt_obj
{
public:
  typedef Slab_cache Self_alloc;

  // undefine default new operator.
  void *operator new(size_t);

  Ram_quota *get_quota() const
  { return _quota; }

  bool can_run() const
  { return _run; }

  void set_run(bool r)
  { _run = r; }

  bool should_be_deleted() const
  { return _manual_delete; }

  void block(Sched_context *scx);
  void deblock(Sched_context *scx);

  virtual void deactivate() = 0;
  virtual void activate() = 0;
  virtual void migrate_away() = 0;
  virtual void migrate_to(Cpu_number) = 0;

  enum Type
  {
    Quant_sc,
    Budget_sc,
    Timer_window_sc,
  };

private:
  Ram_quota *_quota;
  bool _run;
  typedef cxx::Sd_list<Sched_context> Blocked_list;
  Blocked_list _list;
  bool _manual_delete;
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
  class Timeslice_timeout : public Timeout
  {
  public:
    Timeslice_timeout(Quant_sc *sc) : _sc(sc)
    {}

  private:
    bool expired() override;
    Quant_sc *_sc;
  };

  Unsigned64 _quantum;
  Unsigned64 _left;
  Timeslice_timeout _tt;
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

class Timer_window_sc : public Sched_constraint
{
private:
  class Timer_window_sc_timeout : public Timeout
  {
  public:
    Timer_window_sc_timeout(Timer_window_sc *sc) : _sc(sc)
    {}

  private:
    bool expired() override;
    Timer_window_sc *_sc;
  };

  Unsigned64 _start;
  Unsigned64 _duration;
  Timer_window_sc_timeout _timeout;
};

// --------------------------------------------------------------------------
IMPLEMENTATION:

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
#include "thread_object.h"

PUBLIC inline NEEDS[<cstddef>]
void *
Sched_constraint::operator new (size_t, void *b) throw()
{ return b; }

PUBLIC
Sched_constraint::Sched_constraint(Ram_quota *q)
: _quota(q),
  _run(false),
  _manual_delete(false)
{ printf("SC[%p]: created\n", this); }

PUBLIC
Sched_constraint::~Sched_constraint()
{
  printf("SC[%p]: delete\n", this);
  assert(ref_cnt() == 0);
}

PUBLIC
bool
Sched_constraint::put() override
{
  //printf("SC[%p]: initiate deletion (ref_cnt: %ld)\n", this, ref_cnt());
  _manual_delete = (ref_cnt() > 0);
  return !_manual_delete;
}

IMPLEMENT
void
Sched_constraint::block(Sched_context *scx)
{
  assert(scx);
  assert(!in_my_blocked_list(scx));

  Ready_queue::rq.current().ready_dequeue(scx);
  _list.push_back(scx);
}

PRIVATE
bool
Sched_constraint::in_my_blocked_list(Sched_context *scx)
{
  assert(scx);

  for (Sched_context *i : _list)
  {
    if (scx == i)
      return true;
  }

  return false;
}

IMPLEMENT
void
Sched_constraint::deblock(Sched_context *scx)
{
  assert(scx);
  assert(in_my_blocked_list(scx));

  _list.remove(scx);
}

PROTECTED
void
Sched_constraint::wake_up_all_blocked()
{
  // TOMO: we want to requeue all blocked threads on THEIR home cpus
  // maybe with drq to them?
  //panic("wake_up_all_blocked() is tricky");

  Sched_context *scx;

  while (!_list.empty())
  {
    scx = _list.front();
    _list.remove(scx);

    // TOMO:
    // enqueue here somehow
    // what about migration happening here in parallel?

    scx->reset_blocked();
    Ready_queue::rq.current().ready_enqueue(scx);
  }
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
  if (t.words() < 3)
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
    case Sched_constraint::Type::Timer_window_sc:
      res = Timer_window_sc::create(q, t, u);
      break;
    default:
      *err = L4_err::EInval;
      res = nullptr;
      break;
  }

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
  _left(Config::Default_time_slice),
  _tt(this)
{ set_run(true); }

IMPLEMENT
bool
Quant_sc::Timeslice_timeout::expired()
{ return true; }

PUBLIC
void
Quant_sc::deactivate() override
{
  Unsigned64 clock = Timer::system_clock();
  Signed64 left = _tt.get_timeout(clock);
  _tt.reset();

  if (left > 0)
    set_left(left);
  else
    replenish();
}

PUBLIC
void
Quant_sc::activate() override
{
  Unsigned64 clock = Timer::system_clock();
  _tt.set(clock + _left, current_cpu());
}

PUBLIC
void
Quant_sc::migrate_away() override
{}

PUBLIC
void
Quant_sc::migrate_to(Cpu_number) override
{}

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
  printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>> BSC[%p]: deadline hit @ %llu\n", this, Timer::system_clock());
  set_run(false);
  // TOMO: send exception to user thread here
  Thread *t = ::current_thread();
  //extern char leave_by_trigger_exception[];
  //t->do_trigger_exception(t->regs(), leave_by_trigger_exception);
  //t->send_exception(t->regs());
  //t->send_sched_exception(t->regs());
  static_cast<Thread_object *>(t)->ex_regs(~0UL, ~0UL, 0, 0, 0, Thread::Exr_trigger_sched_exception);
}

PRIVATE
bool
Budget_sc::period_expired()
{
  // TOMO: requeue here?
  if (M_SCHEDULER_DEBUG) printf("SCHEDULER> BSC[%p]: period_expired\n", this);
  //printf("SCHEDULER> BSC[%p]: period_expired\n", this);
  replenish();
  calc_and_schedule_next_repl(current_cpu());

  wake_up_all_blocked();

  Context *curr { ::current() };

  if (curr && curr->sched()->contains(this))
  {
    _oob_timeout.reset();
    activate();
  }

  //printf("hello requeue\n");
  //Ready_queue &rq { Ready_queue::rq.current() };
  //rq.requeue(curr);

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

  set_left(max(left, static_cast<Signed64>(0)));
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

static Kmem_slab_t<Timer_window_sc> _timer_window_sc_allocator("Timer_window_sc");

PRIVATE static
Timer_window_sc::Self_alloc *
Timer_window_sc::allocator()
{ return _timer_window_sc_allocator.slab(); }

PUBLIC inline
void
Timer_window_sc::operator delete (void *ptr)
{
  Timer_window_sc *sc = reinterpret_cast<Timer_window_sc *>(ptr);
  allocator()->q_free<Ram_quota>(sc->get_quota(), sc);
}

PUBLIC static
Timer_window_sc *
Timer_window_sc::create(Ram_quota *q, L4_msg_tag t, Utcb const *u)
{
  assert(t.words() == 7);

  Unsigned64 start = u->values[4];
  Unsigned64 duration = u->values[6];

  return create(q, start, duration);
}

PUBLIC static
Timer_window_sc *
Timer_window_sc::create(Ram_quota *q, Unsigned64 s, Unsigned64 d)
{
  void *p = allocator()->q_alloc<Ram_quota>(q);
  return p ? new (p) Timer_window_sc(q, s, d) : 0;
}

PUBLIC
Timer_window_sc::Timer_window_sc(Ram_quota *q, Unsigned64 s, Unsigned64 d)
: Sched_constraint(q),
  _start(s),
  _duration(d),
  _timeout(this)
{
  Unsigned64 now = Timer::system_clock();
  bool in_window;
  in_window = ((now >= s) && (now <= s + d));
  set_run(in_window);
}

PRIVATE
void
Timer_window_sc::calc_and_schedule_next_timeout(Cpu_number target)
{
  Unsigned64 now { Timer::system_clock() };

  if (now >= _start + _duration)
    return;

  Unsigned64 time;

  if (!can_run())
  {
    if (_start < now)
      return;

    assert(!_timeout.is_set());
    time = _start;
  }
  else
  {
    assert(now <= (_start + _duration));
    assert(!_timeout.is_set());

    time = _start + _duration;
  }

  if (M_SCHEDULER_DEBUG) printf("TWSC[%p]: setting timeout @ %llu\n", this, time);
  _timeout.set(time, target);
}

PRIVATE
void
Timer_window_sc::flip_state()
{
  set_run(!can_run());
  _timeout.reset();
  calc_and_schedule_next_timeout(current_cpu());
  if (can_run())
    wake_up_all_blocked();
}

IMPLEMENT
bool
Timer_window_sc::Timer_window_sc_timeout::expired()
{
  Unsigned64 now = Timer::system_clock();
  if (M_SCHEDULER_DEBUG) printf("TWSC[%p]: timeout expired @ %llu\n", _sc, now);
  _sc->flip_state();

  // force reschedule.
  return true;
}

PUBLIC
void
Timer_window_sc::deactivate() override
{}

PUBLIC
void
Timer_window_sc::activate() override
{}

PUBLIC
void
Timer_window_sc::migrate_away() override
{ _timeout.reset(); }

PUBLIC
void
Timer_window_sc::migrate_to(Cpu_number target) override
{ calc_and_schedule_next_timeout(target); }

