/**
 * Ready_queue for scheduling contexts.
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

  int c = 0;

  void set_idle(Sched_context *sc)
  { sc->_prio = Config::Kernel_prio; }

  void enqueue(Sched_context *, bool);
  void dequeue(Sched_context *);
  Sched_context *next_to_run() const;

  void activate(Sched_context *sc)
  { _current_sched = sc; }
  Sched_context *current_sched() const
  { return _current_sched; }

  void set_current_sched(Sched_context *sched);
  void invalidate_sched() { activate(0); }
  bool deblock(Sched_context *sc, Sched_context *crs, bool lazy_q = false);

  void ready_enqueue(Sched_context *sc)
  {
    assert(cpu_lock.test());

    // Don't enqueue threads which are already enqueued
    if (EXPECT_FALSE (sc->in_ready_queue()))
      return;

    enqueue(sc, true);
  }

  void ready_dequeue(Sched_context *sc)
  {
    assert (cpu_lock.test());

    // Don't dequeue threads which aren't enqueued
    if (EXPECT_FALSE (!sc->in_ready_queue()))
      return;

    dequeue(sc);
  }

  void switch_sched(Sched_context *from, Sched_context *to)
  {
    assert (cpu_lock.test());

    // If we're leaving the global timeslice, invalidate it This causes
    // schedule() to select a new timeslice via set_current_sched()
    if (from == current_sched())
      invalidate_sched();

    if (from->in_ready_queue())
      dequeue(from);

    enqueue(to, false);
  }

  Context *schedule_in_progress;

private:
    typedef cxx::Sd_list<Sched_context> Queue;
    Unsigned8 prio_highest { 0 };
    Queue queue[priorities];

    Sched_context *_current_sched;

};

// --------------------------------------------------------------------------
IMPLEMENTATION:

#include "panic.h"
#include <cassert>
#include "cpu_lock.h"
#include "std_macros.h"

DEFINE_PER_CPU Per_cpu<Ready_queue> Ready_queue::rq;

IMPLEMENT inline
Sched_context *
Ready_queue::next_to_run() const
{ return queue[prio_highest].front(); }

/**
 * Enqueue context in ready-list.
 */
IMPLEMENT
void
Ready_queue::enqueue(Sched_context *sc, bool is_current_sched)
{
  assert(cpu_lock.test());

  // Don't enqueue threads which are already enqueued
  if (EXPECT_FALSE (sc->in_ready_queue()))
    return;

  Unsigned8 prio = sc->prio();

  if (prio > prio_highest)
    prio_highest = prio;

  queue[prio].push(sc, is_current_sched ? Queue::Front : Queue::Back);
  c++;
}

/**
 * Remove context from ready-list.
 */
IMPLEMENT inline NEEDS ["cpu_lock.h", <cassert>, "std_macros.h"]
void
Ready_queue::dequeue(Sched_context *sc)
{
  assert (cpu_lock.test());

  // Don't dequeue threads which aren't enqueued
  if (EXPECT_FALSE (!sc->in_ready_queue()))
    return;

  Unsigned8 prio = sc->prio();

  queue[prio].remove(sc);

  while (queue[prio_highest].empty() && prio_highest)
    prio_highest--;
  c--;
}
      
      
PUBLIC inline
void
Ready_queue::requeue(Sched_context *sc)
{
  if (!sc->in_ready_queue())
    enqueue(sc, false);
  else
    queue[sc->prio()].rotate_to(*++Queue::iter(sc));
}
      
PUBLIC inline
void
Ready_queue::deblock_refill(Sched_context *)
{}

/**
 *  * Set currently active global Sched_context.
 *   */
IMPLEMENT
void
Ready_queue::set_current_sched(Sched_context *sched)
{
  assert (sched);
  // Save remainder of previous timeslice or refresh it, unless it had
  // been invalidated
  Timeout * const tt = timeslice_timeout.current();
  Unsigned64 clock = Timer::system_clock();
  if (Sched_context *s = current_sched())
  {
    Signed64 left = tt->get_timeout(clock);
    if (left > 0)
      s->set_left(left);
    else
      s->replenish();

    LOG_SCHED_SAVE(s);
  }

  // Program new end-of-timeslice timeout
  tt->reset();
  tt->set(clock + sched->left(), current_cpu());

  // Make this timeslice current
  activate(sched);

  LOG_SCHED_LOAD(sched);
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
 * \param sc      Sched_context that shall be deblocked.
 * \param crs     Sched_context of the currently running context.
 * \param lazy_q  Queue lazily if applicable.
 *
 * \returns Whether a reschedule is necessary (deblocked scheduling context
 *          can preempt the currently running scheduling context).
 */
IMPLEMENT inline NEEDS[<cassert>]
bool
Ready_queue::deblock(Sched_context *sc, Sched_context *crs, bool lazy_q)
{
  assert(cpu_lock.test());

  Sched_context *cs = current_sched();
  bool res = true;
  if (sc == cs)
  {
    if (crs && crs->dominates(sc))
      res = false;
  }
  else
  {
    deblock_refill(sc);

    if ((EXPECT_TRUE(cs != 0) && cs->dominates(sc))
        || (crs && crs->dominates(sc)))
      res = false;
  }

  if (res && lazy_q)
    return true;

  ready_enqueue(sc);
  return res;
}
