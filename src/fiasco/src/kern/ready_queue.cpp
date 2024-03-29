/**
 * Ready_queue.
 */

// --------------------------------------------------------------------------
INTERFACE:

#include <cxx/dlist>
#include "per_cpu_data.h"
#include "sched_context.h"

class Ready_queue
{
public:
  static Per_cpu<Ready_queue> rq;
  static constexpr auto priorities { 256 };
  int _c = 0;

  void enqueue(Sched_context *, bool);
  void dequeue(Sched_context *);
  Sched_context *next_to_run() const;

  void set_idle(Sched_context *scx) { scx->_prio = Config::Kernel_prio; }
  void activate(Sched_context *scx) { _current = scx; }
  Sched_context *current() const { return _current; }
  void invalidate_current() { activate(nullptr); }

  void set_current(Sched_context *);
  bool deblock(Sched_context *, Sched_context *, bool = false);

  void ready_enqueue(Sched_context *scx)
  {
    if (M_SCHEDULER_DEBUG) printf("SCHEDULER> RQ[%p]: trying to enqueue SCX[%p]\n", this, scx);
    assert(cpu_lock.test());
    // TOMO: maybe we don't need this?
    assert(scx->is_constrained());

    // Don't enqueue threads which are already enqueued
    if (EXPECT_FALSE (scx->is_queued()))
      return;

    enqueue(scx, true);
  }

  void ready_dequeue(Sched_context *scx)
  {
    if (M_SCHEDULER_DEBUG) printf("SCHEDULER> RQ[%p]: trying to dequeue SCX[%p]\n", this, scx);
    assert(cpu_lock.test());

    // Don't dequeue threads which aren't enqueued
    if (EXPECT_FALSE (!scx->is_queued()))
      return;

    dequeue(scx);
  }

  Sched_context *schedule_in_progress;

private:
    typedef cxx::Sd_list<Sched_context> Queue;
    Unsigned8 prio_highest { 0 };
    Queue queue[priorities];

    Sched_context *_current;
};

// --------------------------------------------------------------------------
IMPLEMENTATION:

#include "cpu_lock.h"
#include "panic.h"
#include "std_macros.h"
#include "logdefs.h"

#include <cassert>

DEFINE_PER_CPU Per_cpu<Ready_queue> Ready_queue::rq;

IMPLEMENT inline
Sched_context *
Ready_queue::next_to_run() const
{ return queue[prio_highest].front(); }

/**
 * Enqueue sched_context in ready-list.
 */
IMPLEMENT
void
Ready_queue::enqueue(Sched_context *scx, bool is_current)
{
  assert(cpu_lock.test());

  // Don't enqueue threads which are already enqueued
  if (EXPECT_FALSE (scx->is_queued()))
    return;

  Unsigned8 prio = scx->prio();

  if (prio > prio_highest)
    prio_highest = prio;

  queue[prio].push(scx, is_current? Queue::Front : Queue::Back);

  _c++;
  if (M_SCHEDULER_DEBUG) printf("SCHEDULER> RQ[addr: %p, entries: %d]: enqueue SCX[%p]\n", this, _c, scx);
}

/**
 * Remove context from ready-list.
 */
IMPLEMENT inline NEEDS ["cpu_lock.h", <cassert>, "std_macros.h"]
void
Ready_queue::dequeue(Sched_context *scx)
{
  assert (cpu_lock.test());

  // Don't dequeue threads which aren't enqueued
  if (EXPECT_FALSE (!scx->is_queued()))
    return;

  Unsigned8 prio = scx->prio();

  queue[prio].remove(scx);

  while (queue[prio_highest].empty() && prio_highest)
    prio_highest--;
  _c--;
  if (M_SCHEDULER_DEBUG) printf("SCHEDULER> RQ[addr: %p, entries: %d]: dequeue SCX[%p]\n", this, _c, scx);
}
      
      
PUBLIC inline
void
Ready_queue::requeue(Sched_context *scx)
{
  if (!scx->is_queued())
    enqueue(scx, false);
  else
    queue[scx->prio()].rotate_to(*++Queue::iter(scx));
}
      
PUBLIC inline
void
Ready_queue::deblock_refill(Sched_context *)
{}

/**
 * Set currently active global Sched_context.
 */
IMPLEMENT
void
Ready_queue::set_current(Sched_context *scx)
{
  assert(scx);
  //// Save remainder of previous timeslice or refresh it, unless it had
  //// been invalidated
  //Timeout * const tt = timeslice_timeout.current();
  //Unsigned64 clock = Timer::system_clock();
  //if (Sched_context *s = current())
  //{
  //  Signed64 left = tt->get_timeout(clock);
  //  if (left > 0)
  //    // TOMO: assumption about SC here!
  //    static_cast<Budget_sc *>(s->get_sched_context())->set_left(left);
  //  else
  //  {
  //    //s->get_quant_sc()->replenish();
  //    printf(">>>>>>>>>>>>>>>>>>>>>>>>>>>> BSC[%p]: budget overrun\n", s);
  //    // TOMO: assumption about SC here!
  //    static_cast<Budget_sc *>(s->get_sched_context())->set_left(0);
  //  }

  //  LOG_SCHED_SAVE(s);
  //}

  //// Program new end-of-timeslice timeout
  //tt->reset();
  //if (M_TIMER_DEBUG) printf("TIMER> setting timeslice timeout @ %llu\n", clock + static_cast<Budget_sc *>(c->get_sched_context())->get_left());
  ////tt->set(clock + c->get_budget_sc()->get_left(), current_cpu());
  //// TOMO: assumption about SC here!
  //tt->set(clock + static_cast<Budget_sc *>(c->get_sched_context())->get_left(), current_cpu());

  // Make this timeslice current
  if (_current)
    _current->deactivate();
  scx->activate();
  activate(scx);

  LOG_SCHED_LOAD(scx);
}

/**
 * Deblock the given scheduling context, i.e. add the scheduling context to the
 * ready queue.
 *
 * As an optimization, if requested by setting the `lazy_q` parameter, only adds
 * the deblocked scheduling context to the ready queue if it cannot preempt the
 * currently active scheduling context, i.e. rescheduling is not necessary.
 * Otherwise the caller is responsible to switch to the lazily deblocked
 * scheduling context via `switch_to_locked()`. This is required to ensure that
 * the scheduler does not forget about the scheduling context.
 *
 * \param c       Sched_context that shall be deblocked.
 * \param current Sched_context of the currently running context.
 * \param lazy_q  Queue lazily if applicable.
 *
 * \returns Whether a reschedule is necessary (deblocked scheduling context
 *          can preempt the currently running scheduling context).
 */
IMPLEMENT inline NEEDS[<cassert>]
bool
Ready_queue::deblock(Sched_context *scx, Sched_context *current, bool lazy_q)
{
  assert(cpu_lock.test());

  Sched_context *rq_current = this->current();
  bool res = true;
  if (scx == rq_current)
  {
    if (current && current->dominates(scx))
      res = false;
  }
  else
  {
    deblock_refill(scx);

    if ((EXPECT_TRUE(rq_current != 0) && rq_current->dominates(scx))
        || (current && current->dominates(scx)))
      res = false;
  }

  if (res && lazy_q)
    return true;

  ready_enqueue(scx);
  return res;
}

