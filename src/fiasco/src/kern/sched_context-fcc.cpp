/*
 * Scheduling contexts as first-class citizens.
 */

// --------------------------------------------------------------------------
INTERFACE [sched_fcc]:

#include <cxx/dlist>
#include "member_offs.h"
#include "types.h"
#include "globals.h"
#include "ready_queue_fp.h"
#include "kobject.h"
#include "per_cpu_data.h"

class Context;

class Sched_context
: public cxx::D_list_item,
  public cxx::Dyn_castable<Sched_context, Kobject>
{
  MEMBER_OFFSET();
  friend class Jdb_list_timeouts;
  friend class Jdb_thread_list;
  friend class Sched_ctxts_test;
  friend class Scheduler_test;

  template<typename T>
  friend struct Jdb_thread_list_policy;

  union Sp
  {
    L4_sched_param p;
    L4_sched_param_legacy legacy_fixed_prio;
    L4_sched_param_fixed_prio fixed_prio;
  };

public:
  typedef cxx::Sd_list<Sched_context> Fp_list;

  class Ready_queue_base : public Ready_queue_fp<Sched_context>
  {
  public:
    void activate(Sched_context *s)
    { _current_sched = s; }
    Sched_context *current_sched() const { return _current_sched; }

  private:
    Sched_context *_current_sched;
  };

  Context *context() const { return _context; }
  void set_context(Context *c) { _context = c; }

private:
  unsigned short _prio;
  Unsigned64 _quantum;
  Unsigned64 _left;

  friend class Ready_queue_fp<Sched_context>;

  Context *_context;
  static Per_cpu<Sched_context *> _kernel_sched_context;
};

// --------------------------------------------------------------------------
IMPLEMENTATION [sched_fcc]:

#include <cassert>
#include "cpu_lock.h"
#include "std_macros.h"
#include "config.h"
//#include "context.h"
#include "lock_guard.h"
#include "thread_state.h"
#include "logdefs.h"
#include "types.h"
#include "processor.h"

DEFINE_PER_CPU Per_cpu<Sched_context *> Sched_context::_kernel_sched_context;

/**
 * Constructor
 */
PUBLIC
Sched_context::Sched_context()
: _prio(Config::Default_prio),
  _quantum(Config::Default_time_slice),
  _left(Config::Default_time_slice),
  _context(nullptr) // TOMO: dangerous :(
{}

PUBLIC static inline
Sched_context *
Sched_context::kernel_sched_context(Cpu_number cpu)
{ return _kernel_sched_context.cpu(cpu); }

PUBLIC static inline
void
Sched_context::set_kernel_sched_context(Cpu_number cpu, Sched_context *sc)
{ _kernel_sched_context.cpu(cpu) = sc; }

/**
 * Return priority of Sched_context
 */
PUBLIC inline
unsigned short
Sched_context::prio() const
{
  return _prio;
}

PUBLIC static inline
Mword
Sched_context::sched_classes()
{
  return 1UL << (-L4_sched_param_fixed_prio::Class);
}

PUBLIC static inline
int
Sched_context::check_param(L4_sched_param const *_p)
{
  Sp const *p = reinterpret_cast<Sp const *>(_p);
  switch (p->p.sched_class)
    {
    case L4_sched_param_fixed_prio::Class:
      if (!_p->check_length<L4_sched_param_fixed_prio>())
        return -L4_err::EInval;
      break;

    default:
      if (!_p->is_legacy())
        return -L4_err::ERange;
      break;
    }

  return 0;
}

PUBLIC
void
Sched_context::set(L4_sched_param const *_p)
{
  Sp const *p = reinterpret_cast<Sp const *>(_p);
  if (_p->is_legacy())
    {
      // legacy fixed prio
      _prio = p->legacy_fixed_prio.prio;
      if (p->legacy_fixed_prio.prio > 255)
        _prio = 255;

      _quantum = p->legacy_fixed_prio.quantum;
      if (p->legacy_fixed_prio.quantum == 0)
        _quantum = Config::Default_time_slice;
      return;
    }

  switch (p->p.sched_class)
    {
    case L4_sched_param_fixed_prio::Class:
      _prio = p->fixed_prio.prio;
      if (p->fixed_prio.prio > 255)
        _prio = 255;

      _quantum = p->fixed_prio.quantum;
      if (p->fixed_prio.quantum == 0)
        _quantum = Config::Default_time_slice;
      break;

    default:
      assert(false && "Missing check_param()?");
      break;
    }
}

/**
 * Return remaining time quantum of Sched_context
 */
PUBLIC inline
Unsigned64
Sched_context::left() const
{
  return _left;
}

PUBLIC inline NEEDS[Sched_context::set_left]
void
Sched_context::replenish()
{ set_left(_quantum); }

/**
 * Set remaining time quantum of Sched_context
 */
PUBLIC inline
void
Sched_context::set_left(Unsigned64 left)
{
  _left = left;
}


/**
 * Check if Context is in ready-list.
 * @return 1 if thread is in ready-list, 0 otherwise
 */
PUBLIC inline
Mword
Sched_context::in_ready_list() const
{
  printf("sc: in_ready_list not implemented\n");
  return Fp_list::in_list(this);
}

PUBLIC inline
bool
Sched_context::dominates(Sched_context *sc)
{ return prio() > sc->prio(); }

PUBLIC
void
Sched_context::schedule()
{
  panic("sc: schedule not implemented\n");
  //auto guard = lock_guard(cpu_lock);
  //assert (!rq.current().schedule_in_progress);

  //// we give up the CPU as a helpee, so we have no helper anymore
  //if (EXPECT_FALSE(_context->helper() != _context))
  //  _context->set_helper(Context::Not_Helping);

  //// if we are a thread on a foreign CPU we must ask the kernel context to
  //// schedule for us
  //Cpu_number current_cpu = ::current_cpu();
  //while (EXPECT_FALSE(current_cpu != access_once(&(_context->_home_cpu))))
  //  {
  //    Context *kc = Context::kernel_context(current_cpu);
  //    assert (_context != kc);

  //    // flag that we need to schedule
  //    kc->state_add_dirty(Thread_need_resched);
  //    switch (_context->switch_exec_locked(kc, Context::Ignore_Helping))
  //      {
  //      case Context::Switch::Ok:
  //        return;
  //      case Context::Switch::Resched:
  //        current_cpu = ::current_cpu();
  //        continue;
  //      case Context::Switch::Failed:
  //        assert (false);
  //        continue;
  //      }
  //  }

  //// now, we are sure that a thread on its home CPU calls schedule.
  //CNT_SCHEDULE;

  //// Ensure only the current thread calls schedule
  //assert (_context == current());

  //Ready_queue *rq = &(this->rq.current());

  //// Enqueue current thread into ready-list to schedule correctly
  //update_ready_list();

  //// Select a thread for scheduling.
  //Context *next_to_run;

  //for (;;)
  //  {
  //    next_to_run = rq->next_to_run()->context();

  //    // Ensure ready-list sanity
  //    assert (next_to_run);

  //    if (EXPECT_FALSE(!(next_to_run->state() & Thread_ready_mask)))
  //      rq->ready_dequeue(next_to_run->sched());
  //    else switch (_context->schedule_switch_to_locked(next_to_run))
  //      {
  //      default:
  //      case Context::Switch::Ok:      return;   // ok worked well
  //      case Context::Switch::Failed:  break;    // not migrated, need preemption point
  //      case Context::Switch::Resched:
  //        {
  //          Cpu_number n = ::current_cpu();
  //          if (n != current_cpu)
  //            {
  //              current_cpu = n;
  //              rq = &(this->rq.current());
  //            }
  //        }
  //        continue; // may have been migrated...
  //      }

  //    rq->schedule_in_progress = _context;
  //    Proc::preemption_point();
  //    if (EXPECT_TRUE(current_cpu == ::current_cpu()))
  //      rq->schedule_in_progress = 0;
  //    else
  //      return; // we got migrated and selected on our new CPU, so we may run
  //  }

}

PUBLIC
void
Sched_context::schedule_if(bool s)
{
  (void)s;
  panic("sc: schedule_if not implemented\n");
  //if (!s || rq.current().schedule_in_progress)
  //  return;

  //_context->schedule();
}

// queue operations

// XXX for now, synchronize with global kernel lock
//-

/**
 * Enqueue current() if ready to fix up ready-list invariant.
 */
PUBLIC //inline
void
Sched_context::update_ready_list()
{
  panic("sc: update_ready_list not implemented\n");
  //assert (_context == current());

  //if ((_context->state() & Thread_ready_mask) && left())
  //  rq.current().ready_enqueue(this);
}

PUBLIC
void
Sched_context::invoke(L4_obj_ref /*self*/, L4_fpage::Rights rights,
                      Syscall_frame *f, Utcb *utcb) override
{
  (void)rights;
  (void)f;
  (void)utcb;
  return;
}

namespace {

static Kobject_iface * FIASCO_FLATTEN
sched_context_factory(Ram_quota *q, Space *, L4_msg_tag, Utcb const *, int *err)
{
  size_t size = sizeof(Sched_context);
  Auto_quota<Ram_quota> quota(q, size);

  if (EXPECT_FALSE(!quota))
    return 0;

  Kmem_alloc *a = Kmem_alloc::allocator();
  void *nq = a->q_alloc(q, Bytes(size));
  if (EXPECT_FALSE(!nq))
    return 0;

  quota.release();
  *err = L4_err::ENomem;
  return new (nq) Sched_context();
}

static inline void __attribute__((constructor)) FIASCO_INIT
register_factory()
{
  Kobject_iface::set_factory(L4_msg_tag::Label_sched_context,
                             sched_context_factory);
}

}

// vorstellung:
//
// context.cpp:
// ============
// # include "sched_context.h"
// class Context { ... };
//
// sched_context.cpp:
// ==================
// #include "kobject.h"
// class Context;
// class Sched_context : public kobject { ... };
