// --------------------------------------------------------------------------
INTERFACE:

#include "member_offs.h"
#include "types.h"
#include "globals.h"
#include "spin_lock.h"
//#include "sched_constraint.h"

#include <cxx/static_vector>
#include <cxx/dlist>

class Sched_constraint;

class Sched_context : public cxx::D_list_item
{
  MEMBER_OFFSET();
  friend class Jdb_list_timeouts;
  friend class Jdb_thread_list;
  friend class Sched_ctxts_test;
  friend class Scheduler_test;
  friend class Ready_queue;
  friend class Context;

  template<typename T>
  friend struct Jdb_thread_list_policy;

  struct L4_sched_param_fixed_prio : public L4_sched_param
  {
    enum : Smword { Class = -1 };
    Mword quantum;
    unsigned short prio;
  };

  union Sp
  {
    L4_sched_param p;
    L4_sched_param_legacy legacy_fixed_prio;
    L4_sched_param_fixed_prio fixed_prio;
  };

public:
  Context *context() const { return context_of(this); }

private:
  Unsigned8 _prio;
  Spin_lock<> _lock;
  Sched_constraint *__scs[Config::Scx_max_sc] = { nullptr };
  typedef cxx::static_vector<Sched_constraint *, unsigned> Sc_list;
  Sc_list _list;
  Sched_constraint *_blocked_by;
};

// --------------------------------------------------------------------------
IMPLEMENTATION:

#include "cpu_lock.h"
#include "std_macros.h"
#include "config.h"
#include "lock_guard.h"
#include "sched_constraint.h"

#include <cassert>

/**
 * Constructor
 */
PUBLIC
Sched_context::Sched_context()
: _prio(Config::Default_prio),
  _lock(Spin_lock<>::Unlocked),
  _list(&__scs[0], Config::Scx_max_sc),
  _blocked_by(nullptr)
{}

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
  Sp const *p { reinterpret_cast<Sp const *>(_p) };
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

/**
 * Check if Context is in ready-list.
 * @return 1 if thread is in ready-list, 0 otherwise
 */
PUBLIC inline
bool
Sched_context::is_queued() const
{
  return cxx::Sd_list<Sched_context>::in_list(this);
}

PUBLIC inline
bool
Sched_context::dominates(Sched_context *sc) const
{ return prio() > sc->prio(); }

PUBLIC
bool
Sched_context::is_constrained() const
{
  for (Sched_constraint *sc : _list)
  {
    if (sc)
      return true;
  }

  return false;
}

PUBLIC inline
bool
Sched_context::is_blocked() const
{
  return _blocked_by != nullptr;
}

PUBLIC inline
Sched_constraint *
Sched_context::blocked_by() const
{
  return _blocked_by;
}

PUBLIC inline
void
Sched_context::reset_blocked()
{
  _blocked_by = nullptr;
}

PUBLIC
bool
Sched_context::can_run()
{
  return is_blocked() ? false : check_sc_list();
}

PRIVATE
bool
Sched_context::check_sc_list()
{
  for (Sched_constraint *sc : _list)
  {
    if (!sc)
      continue;

    if (sc->can_run())
      continue;

    sc->block(this);
    _blocked_by = sc;
    return false;
  }

  return true;
}

PUBLIC
void
Sched_context::deactivate() const
{
  for (Sched_constraint *sc : _list)
  {
    if (!sc)
      continue;

    sc->deactivate();
  }
}

PUBLIC
void
Sched_context::activate() const
{
  for (Sched_constraint *sc : _list)
  {
    if (!sc)
      continue;

    sc->activate();
  }
}

PUBLIC
void
Sched_context::migrate_away() const
{
  for (Sched_constraint *sc : _list)
  {
    if (!sc)
      continue;

    sc->migrate_away();
  }
}

PUBLIC
void
Sched_context::migrate_to(Cpu_number cpu) const
{
  for (Sched_constraint *sc : _list)
  {
    if (!sc)
      continue;

    sc->migrate_to(cpu);
  }
}

PUBLIC
bool
Sched_context::contains(Sched_constraint *sc) const
{
  for (Sched_constraint *i : _list)
  {
    if (sc == i)
      return true;
  }

  return false;
}

PUBLIC
void
Sched_context::print() const
{
  printf("SCX[%p]\n", this);
  printf("|- prio: %d\n", _prio);
  printf("|- list: ");
  for (Sched_constraint *sc : _list)
    printf("SC[%p]-", sc);
  printf(">\n");
}

PUBLIC
bool
Sched_context::attach(Sched_constraint *sc)
{
  assert(sc);

  auto l { lock_guard(_lock) };

  assert(!contains(sc));

  for (Sched_constraint *&i : _list)
  {
    if (i)
      continue;

    i = sc;
    sc->inc_ref();
    return true;
  }

  return false;
}

PUBLIC
bool
Sched_context::detach(Sched_constraint *sc)
{
  assert(sc);

  auto l { lock_guard(_lock) };

  assert(contains(sc));

  for (Sched_constraint *&i : _list)
  {
    if (sc != i)
      continue;

    i = nullptr;
    sc->dec_ref();

    if (sc == _blocked_by)
    {
      sc->deblock(this);
      _blocked_by = nullptr;
    }

    return true;
  }

  return false;
}

