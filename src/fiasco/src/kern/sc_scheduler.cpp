/*
 * Scheduler for Scheduling Contexts.
 */

//---------------------------------------------------------------------------
INTERFACE:

#include <cxx/dlist>
#include "per_cpu_data.h"
#include "sched_context.h"

class SC_Scheduler
{
public:
  static constexpr auto priorities { 256 };

  static Sched_context *get_current();
  static void set_current(Sched_context *);
  static void deblock(Sched_context *);
  static void schedule(bool);

// TOMO: ideally, we do not want to expose the ready_queue to the outside world,
// because this enables complex side-effects.
// Normally, every interaction should go through SC_Scheduler.
// BUT: migration code needs to manipulate the queue directly,
// so we expose it for now. maybe refractor this later?
public:
  class Ready_queue
  {
  public:
    void enqueue(Sched_context *);
    void dequeue(Sched_context *);
    Sched_context *next();
    int c = 0;

  private:
    typedef cxx::Sd_list<Sched_context> Queue;
    Unsigned8 prio_highest { 0 };
    Queue queue[priorities];
  };

  static Per_cpu<Sched_context *> current;
  static Per_cpu<Ready_queue> rq;
};

//---------------------------------------------------------------------------
IMPLEMENTATION:

#include "panic.h"
#include <cassert>
#include "cpu_lock.h"
#include "std_macros.h"

DEFINE_PER_CPU Per_cpu<Sched_context *> SC_Scheduler::current;
DEFINE_PER_CPU Per_cpu<SC_Scheduler::Ready_queue> SC_Scheduler::rq;

IMPLEMENT static
Sched_context *
SC_Scheduler::get_current()
{ return SC_Scheduler::current.current(); }

IMPLEMENT static
void
SC_Scheduler::set_current(Sched_context *sc)
{
  // TOMO: synchronization!?!?
  assert(sc);

    // Program new end-of-timeslice timeout.
  Timeout * const tt { timeslice_timeout.current() };
  Unsigned64 clock { Timer::system_clock() };
  tt->reset();
  if (M_TIMER_DEBUG) printf("TIMER> setting timeslice timeout @ %llu\n", clock + sc->left());
  tt->set(clock + sc->left(), current_cpu());

  // Update left of current.
  Sched_context *&current { SC_Scheduler::current.current() };
  if (current)
  {
    Signed64 left = tt->get_timeout(clock);
    if (left > 0)
      current->set_left(left);
    else
      current->replenish();
  }

  // Make sc current.
  current = sc;
}

IMPLEMENT static
void
SC_Scheduler::deblock(Sched_context *sc)
{
  Ready_queue &rq { SC_Scheduler::rq.current() };
  rq.enqueue(sc);
}

IMPLEMENT static
void
SC_Scheduler::schedule(bool blocked)
{
  Sched_context *&current { SC_Scheduler::current.current() };
  Sched_context *old, *next;
  Ready_queue &rq { SC_Scheduler::rq.current() };

  assert(current);
  assert(blocked || !current->in_ready_queue());

  // TOMO: why do we need left anyway?
  //current->replenish();

  if (EXPECT_TRUE (!blocked))
    rq.enqueue(current);


  //for (;;)
  //{
  //printf("we schedule now... (RQ has %d entries.)\n", rq.c);
  old = current;
  next = rq.next();
  set_current(next);
  // TOMO: ugly :(
  // don't use current from here on.
  auto res { old->context()->switch_exec_locked(next->context()) };
  (void) res;
  //panic("sc_scheduler: schedule not implemented yet");
  //}

}

IMPLEMENT
void
SC_Scheduler::Ready_queue::enqueue(Sched_context *sc)
{
  assert(cpu_lock.test());
  assert(sc);
  assert(sc->prio() < priorities);

  if (EXPECT_FALSE (Queue::in_list(sc)))
    return;

  if (sc->prio() > prio_highest)
    prio_highest = sc->prio();

  queue[sc->prio()].push_back(sc);
  c++;
}

IMPLEMENT
void
SC_Scheduler::Ready_queue::dequeue(Sched_context *sc)
{
  assert(cpu_lock.test());
  assert(sc);
  assert(sc->prio() < priorities);

  if (EXPECT_FALSE (!Queue::in_list(sc)))
    return;

  queue[sc->prio()].remove(sc);

  while (queue[prio_highest].empty() && prio_highest)
    prio_highest--;

  c--;
}

IMPLEMENT
Sched_context *
SC_Scheduler::Ready_queue::next()
{
  assert(cpu_lock.test());

  // TOMO: possible optimisation here.
  // (avoid double access to queue)
  Sched_context *const sc { queue[prio_highest].front() };
  dequeue(sc);

  return sc;
}

