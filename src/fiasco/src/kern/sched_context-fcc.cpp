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
class Prio_sc;
class Quant_sc;

class Sched_context
: public cxx::Dyn_castable<Sched_context, Kobject>,
  public Ref_cnt_obj
{
public:
  typedef Slab_cache Self_alloc;
  // undefine default new operator.
  void *operator new(size_t);

private:
  enum Operation
  { Test = 0, };

  Ram_quota *_quota;
};

class Prio_sc
: public Sched_context,
  public cxx::D_list_item
{
  friend class Ready_queue;

  union Sp
  {
    L4_sched_param p;
    L4_sched_param_legacy legacy_fixed_prio;
    L4_sched_param_fixed_prio fixed_prio;
  };

public:
  bool in_ready_queue() const
  { return cxx::Sd_list<Prio_sc>::in_list(this); }

  bool dominates(Prio_sc *p) const
  {
    assert(p);
    return _prio > p->get_prio();
  }

  Context *get_context() const
  { return _context; }

  void set_context(Context *c)
  { _context = c; }

  Unsigned8 get_prio() const
  { return _prio; }

  void set_prio(Unsigned8 p)
  { _prio = p; }

  Quant_sc *get_quant_sc() const
  { return _quant_sc; }

  void set_quant_sc(Quant_sc *q)
  {
    assert(q);
    _quant_sc = q;
  }

private:
  Context *_context;
  Unsigned8 _prio;
  Quant_sc *_quant_sc;
};

class Quant_sc : public Sched_context
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

//PRIVATE static
//Sched_context::Self_alloc *
//Sched_context::allocator()
//{ return _sched_context_allocator.slab(); }

//PUBLIC inline
//void
//Sched_context::operator delete (void *ptr)
//{ allocator()->free(reinterpret_cast<Sched_context *>(ptr)); }

PUBLIC inline NEEDS[<cstddef>]
void *
Sched_context::operator new (size_t, void *b) throw()
{ return b; }

PUBLIC
Sched_context::Sched_context(Ram_quota *q)
: _quota(q)
{}

PUBLIC
Sched_context::~Sched_context()
{ printf("SC[%p]: delete\n", this); }

//PUBLIC static
//Sched_context *
//Sched_context::create(Ram_quota *q)
//{
//  void *p = allocator()->q_alloc<Ram_quota>(q);
//  return p ? new (p) Sched_context(q) : 0;
//}

PUBLIC static inline
Mword
Sched_context::sched_classes()
{
  return 1UL << (-L4_sched_param_fixed_prio::Class);
}

PRIVATE
L4_msg_tag
Sched_context::test()
{
  printf("SC[%p]: SYSCALL!\n", this);
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
  panic("creating SCs from userspace is not supported yet.");
  //return Sched_context::create(q);
}

static inline void __attribute__((constructor)) FIASCO_INIT
register_factory()
{
  Kobject_iface::set_factory(L4_msg_tag::Label_sched_context,
                             sched_context_factory);
}

}

static Kmem_slab_t<Prio_sc> _prio_sc_allocator("Prio_sc");

PRIVATE static
Prio_sc::Self_alloc *
Prio_sc::allocator()
{ return _prio_sc_allocator.slab(); }

PUBLIC inline
void
Prio_sc::operator delete (void *ptr)
{ allocator()->free(reinterpret_cast<Prio_sc *>(ptr)); }

PUBLIC static
Prio_sc *
Prio_sc::create(Ram_quota *q)
{
  void *p = allocator()->q_alloc<Ram_quota>(q);
  return p ? new (p) Prio_sc(q) : 0;
}

PUBLIC
Prio_sc::Prio_sc(Ram_quota *q)
: Sched_context(q),
  _context(nullptr),
  _prio(Config::Default_prio),
  _quant_sc(nullptr)
{}

PUBLIC static inline
int
Prio_sc::check_param(L4_sched_param const *_p)
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
Prio_sc::set(L4_sched_param const *_p)
{
  if (M_SCHEDULER_DEBUG) printf("SCHEDULER> Prio_sc[%p]: set\n", this);
  Sp const *p = reinterpret_cast<Sp const *>(_p);
  if (_p->is_legacy())
  {
    // legacy fixed prio
    _prio = p->legacy_fixed_prio.prio;
    if (p->legacy_fixed_prio.prio > 255)
      _prio = 255;

    get_quant_sc()->set_quantum(p->legacy_fixed_prio.quantum);
    if (p->legacy_fixed_prio.quantum == 0)
      get_quant_sc()->set_quantum(Config::Default_time_slice);
    return;
  }

  switch (p->p.sched_class)
  {
    case L4_sched_param_fixed_prio::Class:
      _prio = p->fixed_prio.prio;
      if (p->fixed_prio.prio > 255)
        _prio = 255;

      get_quant_sc()->set_quantum(p->fixed_prio.quantum);
      if (p->fixed_prio.quantum == 0)
        get_quant_sc()->set_quantum(Config::Default_time_slice);
      break;

    default:
      assert(false && "Missing check_param()?");
      break;
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
{ allocator()->free(reinterpret_cast<Quant_sc *>(ptr)); }

PUBLIC static
Quant_sc *
Quant_sc::create(Ram_quota *q)
{
  void *p = allocator()->q_alloc<Ram_quota>(q);
  return p ? new (p) Quant_sc(q) : 0;
}

PUBLIC
Quant_sc::Quant_sc(Ram_quota *q)
: Sched_context(q),
  _quantum(Config::Default_time_slice),
  _left(Config::Default_time_slice)
{}
