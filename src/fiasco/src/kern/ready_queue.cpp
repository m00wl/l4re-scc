/**
 * Ready_queue.
 */

// --------------------------------------------------------------------------
INTERFACE:

#include <cxx/dlist>
#include "per_cpu_data.h"
#include "context.h"
#include "sched_context.h"

class Ready_queue
{
public:
  static Per_cpu<Ready_queue> rq;
  static constexpr auto priorities { 256 };
  int _c = 0;

  void enqueue(Context *, bool);
  void dequeue(Context *);
  Context *next_to_run() const;

  void set_idle(Context *c) { c->_prio = Config::Kernel_prio; }
  void activate(Context *c) { _current = c; }
  Context *current() const { return _current; }
  void invalidate_current() { activate(nullptr); }

  void set_current(Context *c);
  bool deblock(Context *c, Context *current, bool lazy_q = false);

  void ready_enqueue(Context *c)
  {
    assert(cpu_lock.test());
    assert(c->get_sched_context());

    // Don't enqueue threads which are already enqueued
    if (EXPECT_FALSE (c->in_ready_queue()))
      return;

    enqueue(c, true);
  }

  void ready_dequeue(Context *c)
  {
    assert (cpu_lock.test());

    // Don't dequeue threads which aren't enqueued
    if (EXPECT_FALSE (!c->in_ready_queue()))
      return;

    dequeue(c);
  }

  void switch_sched(Context *from, Context *to)
  {
    assert (cpu_lock.test());

    // If we're leaving the global timeslice, invalidate it This causes
    // schedule() to select a new timeslice via set_current_sched()
    if (from == current())
      invalidate_current();

    if (from->in_ready_queue())
      dequeue(from);

    enqueue(to, false);
  }

  Context *schedule_in_progress;

private:
    typedef cxx::Sd_list<Context> Queue;
    Unsigned8 prio_highest { 0 };
    Queue queue[priorities];

    Context *_current;
};

// --------------------------------------------------------------------------
IMPLEMENTATION:

#include "panic.h"
#include <cassert>
#include "cpu_lock.h"
#include "std_macros.h"

DEFINE_PER_CPU Per_cpu<Ready_queue> Ready_queue::rq;

IMPLEMENT inline
Context *
Ready_queue::next_to_run() const
{ return queue[prio_highest].front(); }

/**
 * Enqueue context in ready-list.
 */
IMPLEMENT
void
Ready_queue::enqueue(Context *c, bool is_current)
{
  assert(cpu_lock.test());

  // Don't enqueue threads which are already enqueued
  if (EXPECT_FALSE (c->in_ready_queue()))
    return;

  Unsigned8 prio = c->get_prio();

  if (prio > prio_highest)
    prio_highest = prio;

  queue[prio].push(c, is_current? Queue::Front : Queue::Back);
  _c++;
  if (M_SCHEDULER_DEBUG) printf("SCHEDULER> RQ[addr: %p, entries: %d]: enqueue C[%p]\n", this, _c, c);
}

/**
 * Remove context from ready-list.
 */
IMPLEMENT inline NEEDS ["cpu_lock.h", <cassert>, "std_macros.h"]
void
Ready_queue::dequeue(Context *c)
{
  assert (cpu_lock.test());

  // Don't dequeue threads which aren't enqueued
  if (EXPECT_FALSE (!c->in_ready_queue()))
    return;

  Unsigned8 prio = c->get_prio();

  queue[prio].remove(c);

  while (queue[prio_highest].empty() && prio_highest)
    prio_highest--;
  _c--;
  if (M_SCHEDULER_DEBUG) printf("SCHEDULER> RQ[addr:%p, entries: %d]: dequeue C[%p]\n", this, _c, c);
}
      
      
PUBLIC inline
void
Ready_queue::requeue(Context *c)
{
  if (!c->in_ready_queue())
    enqueue(c, false);
  else
    queue[c->get_prio()].rotate_to(*++Queue::iter(c));
}
      
PUBLIC inline
void
Ready_queue::deblock_refill(Context *)
{}

/**
 * Set currently active global Context.
 */
IMPLEMENT
void
Ready_queue::set_current(Context *c)
{
  assert (c);
  //// Save remainder of previous timeslice or refresh it, unless it had
  //// been invalidated
  //Timeout * const tt = timeslice_timeout.current();
  //Unsigned64 clock = Timer::system_clock();
  //if (Context *s = current())
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
  activate(c);

  LOG_SCHED_LOAD(c);
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
 * \param c       Context that shall be deblocked.
 * \param current Context of the currently running context.
 * \param lazy_q  Queue lazily if applicable.
 *
 * \returns Whether a reschedule is necessary (deblocked scheduling context
 *          can preempt the currently running scheduling context).
 */
IMPLEMENT inline NEEDS[<cassert>]
bool
Ready_queue::deblock(Context *c, Context *current, bool lazy_q)
{
  assert(cpu_lock.test());

  Context *cs = this->current();
  bool res = true;
  if (c == cs)
  {
    if (current && current->dominates(c))
      res = false;
  }
  else
  {
    deblock_refill(c);

    if ((EXPECT_TRUE(cs != 0) && cs->dominates(c))
        || (current && current->dominates(c)))
      res = false;
  }

  if (res && lazy_q)
    return true;

  ready_enqueue(c);
  return res;
}
