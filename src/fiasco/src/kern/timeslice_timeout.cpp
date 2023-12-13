
INTERFACE:

#include "timeout.h"

class Timeslice_timeout : public Timeout
{
};

IMPLEMENTATION:

#include <cassert>
#include "globals.h"
#include "sched_context.h"
#include "std_macros.h"
#include "sc_scheduler.h"
#include "ready_queue.h"

/* Initialize global valiable timeslice_timeout */
DEFINE_PER_CPU Per_cpu<Timeout *> timeslice_timeout;
DEFINE_PER_CPU static Per_cpu<Timeslice_timeout> the_timeslice_timeout(Per_cpu_data::Cpu_num);

PUBLIC
Timeslice_timeout::Timeslice_timeout(Cpu_number cpu)
{
  timeslice_timeout.cpu(cpu) = this;
}


/**
 * Timeout expiration callback function
 * @return true to force a reschedule
 */
PRIVATE
bool
Timeslice_timeout::expired() override
{
  //Sched_context::Ready_queue &rq = Sched_context::rq.current();
  Ready_queue &rq { Ready_queue::rq.current() };
  Prio_sc *sched = rq.current_sched();

  if (sched)
  {
    sched->get_budget_sc()->timeslice_expired();
    // we dequeue the SC here, because we are out of budget and need to make room for other lower-prio threads.
    // the SCs repl timeout will enqueue again later.
    //sched->get_quant_sc()->replenish();
    //rq.requeue(sched);
    rq.invalidate_sched();
    rq.ready_dequeue(sched);
  }

  return true;
}
