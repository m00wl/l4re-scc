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
  //friend class Jdb_list_timeouts;
  //friend class Jdb_thread_list;
  //friend class Sched_ctxts_test;
  //friend class Scheduler_test;

  //template<typename T>
  //friend struct Jdb_thread_list_policy;
  friend class Ready_queue;

  union Sp
  {
    L4_sched_param p;
    L4_sched_param_legacy legacy_fixed_prio;
    L4_sched_param_fixed_prio fixed_prio;
  };

  typedef Slab_cache Self_alloc;

public:
  //typedef cxx::Sd_list<Sched_context> Fp_list;

  //class Ready_queue_base : public Ready_queue_fp<Sched_context>
  //{
  //public:
  //  void activate(Sched_context *s)
  //  { _current_sched = s; }
  //  Sched_context *current_sched() const { return _current_sched; }

  //private:
  //  Sched_context *_current_sched;
  //};

  Context *context() const { return _context; }
  void set_context(Context *c) { _context = c; }

  // undefine default new operator.
  void *operator new(size_t);

private:
  Unsigned8 _prio;
  Unsigned64 _quantum;
  Unsigned64 _left;

  //friend class Ready_queue_fp<Sched_context>;

  Context *_context;
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

/**
 * Constructor
 */
PUBLIC
Sched_context::Sched_context()
: _prio(Config::Default_prio),
  _quantum(Config::Default_time_slice),
  _left(Config::Default_time_slice),
  _context(nullptr) //TOMO: this is highly unsafe.
{}

PUBLIC
Sched_context::Sched_context(Unsigned8 prio)
: _prio(prio),
  _quantum(Config::Default_time_slice),
  _left(Config::Default_time_slice),
  _context(nullptr) //TOMO: this is highly unsafe.
{}

PUBLIC
Sched_context::Sched_context(Unsigned8 prio, Unsigned64 quantum)
: _prio(prio),
  _quantum(quantum),
  _left(quantum),
  _context(nullptr) //TOMO: this is highly unsafe.
{}

PUBLIC
Sched_context::~Sched_context()
{
  printf("sched_context was deleted\n");
}

PUBLIC //inline
void
Sched_context::operator delete (void *ptr)
{
  Sched_context *sc = reinterpret_cast<Sched_context *>(ptr);
  //LOG_TRACE("Kobject delete", "del", current(), Log_destroy,
  //          l->id = t->dbg_id();
  //          l->obj = t;
  //          l->type = cxx::Typeid<Sched_context>::get();
  //          l->ram = t->ram_quota()->current());

  allocator()->free(sc);
}


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
Sched_context::create(Ram_quota *q, Unsigned8 prio = Config::Default_prio, Unsigned64 quantum = Config::Default_time_slice)
{
  Auto_quota<Ram_quota> quota(q, sizeof(Sched_context));

  if (EXPECT_FALSE(!quota))
    return 0;

  void *nq = Sched_context::allocator()->alloc();
  if (EXPECT_FALSE(!nq))
    return 0;

  quota.release();
  return new (nq) Sched_context(prio, quantum);
}

PUBLIC static inline
Sched_context *
Sched_context::get_kernel_sc()
{ return Sched_context::kernel_sc.current(); }

PUBLIC static inline
void
Sched_context::set_kernel_sc(Sched_context *sc)
{ Sched_context::kernel_sc.current() = sc; }

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
