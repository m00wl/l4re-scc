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
#include "ref_obj.h"

class Context;

class Sched_context
: public cxx::D_list_item,
  public cxx::Dyn_castable<Sched_context, Kobject>,
  public Ref_cnt_obj
{
  MEMBER_OFFSET();
  friend class Ready_queue;

  union Sp
  {
    L4_sched_param p;
    L4_sched_param_legacy legacy_fixed_prio;
    L4_sched_param_fixed_prio fixed_prio;
  };

  typedef Slab_cache Self_alloc;

public:
  Context *context() const { return _context; }
  void set_context(Context *c) { _context = c; }

  // undefine default new operator.
  void *operator new(size_t);

private:
  enum Operation
  {
    Test = 0,
  };

  Unsigned8 _prio;
  Unsigned64 _quantum;
  Unsigned64 _left;

  Context *_context;
  Ram_quota *_quota;

  static Per_cpu<Sched_context *> kernel_sc;
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

DEFINE_PER_CPU Per_cpu<Sched_context *> Sched_context::kernel_sc;

PUBLIC
Sched_context::Sched_context(Ram_quota *q)
: _prio(Config::Default_prio),
  _quantum(Config::Default_time_slice),
  _left(Config::Default_time_slice),
  _context(nullptr), //TOMO: this is highly unsafe.
  _quota(q)
{}

PUBLIC
Sched_context::~Sched_context()
{
  printf("WARNING: sched_context was deleted [was attached to thread %p].\n", this->context());
}

PUBLIC inline
void
Sched_context::operator delete (void *ptr)
{ allocator()->free(reinterpret_cast<Sched_context *>(ptr)); }


PUBLIC inline NEEDS[<cstddef>]
void *
Sched_context::operator new (size_t, void *b) throw()
{ return b; }

static Kmem_slab_t<Sched_context> _sched_context_allocator("Sched_context");

PRIVATE static
Sched_context::Self_alloc *
Sched_context::allocator()
{ return _sched_context_allocator.slab(); }

PUBLIC static
Sched_context *
Sched_context::create(Ram_quota *q)
{
  void *p = allocator()->q_alloc<Ram_quota>(q);
  if (!p)
    return 0;

  return new (p) Sched_context(q);
}

PUBLIC static inline
Sched_context *
Sched_context::get_kernel_sc()
{ return Sched_context::kernel_sc.current(); }

PUBLIC static inline
void
Sched_context::set_kernel_sc(Cpu_number cpu, Sched_context *sc)
{ Sched_context::kernel_sc.cpu(cpu) = sc; }

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
  if (M_SCHEDULER_DEBUG) printf("SCHEDULER> changing parameters of this sched_context: %p\n", this);
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

PUBLIC inline
Mword
Sched_context::in_ready_queue() const
{
  return cxx::Sd_list<Sched_context>::in_list(this);
}

PUBLIC inline
bool
Sched_context::dominates(Sched_context *sc)
{ return prio() > sc->prio(); }

PRIVATE
L4_msg_tag
Sched_context::test()
{
  printf("SC SYSCALL!\n");
  return commit_result(0);
}

PUBLIC
void
Sched_context::invoke(L4_obj_ref self, L4_fpage::Rights rights,
                      Syscall_frame *f, Utcb *utcb) override
{
  (void)rights;

  L4_msg_tag res(L4_msg_tag::Schedule);

  if (EXPECT_TRUE(self.op() & L4_obj_ref::Ipc_send))
  {
    switch (utcb->values[0])
    {
      case Test: res = test(); break;
      default:   res = commit_result(-L4_err::ENosys); break;
    }
  }

  f->tag(res);
}

namespace {

static Kobject_iface * FIASCO_FLATTEN
sched_context_factory(Ram_quota *q, Space *, L4_msg_tag, Utcb const *, int *err)
{
  *err = L4_err::ENomem;
  return Sched_context::create(q);
}

static inline void __attribute__((constructor)) FIASCO_INIT
register_factory()
{
  Kobject_iface::set_factory(L4_msg_tag::Label_sched_context,
                             sched_context_factory);
}

}
