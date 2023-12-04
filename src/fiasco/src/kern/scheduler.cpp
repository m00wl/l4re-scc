INTERFACE:

#include "context.h"
#include "icu_helper.h"
#include "types.h"

class Scheduler : public Icu_h<Scheduler>, public Irq_chip_soft
{
  friend class Scheduler_test;

  typedef Icu_h<Scheduler> Icu;

public:
  enum Operation
  {
    Info       = 0,
    Run_thread = 1,
    Idle_time  = 2,
    Run_thread3 = 3,
  };

  static Scheduler scheduler;
private:
  Irq_base *_irq;

  L4_RPC(Info,      sched_info, (L4_cpu_set_descr set, Mword *rm,
                                 Mword *max_cpus, Mword *sched_classes));
  L4_RPC(Idle_time, sched_idle, (L4_cpu_set cpus, Cpu_time *time));
};

// ----------------------------------------------------------------------------
IMPLEMENTATION:

#include "thread_object.h"
#include "l4_buf_iter.h"
#include "l4_types.h"
#include "entry_frame.h"
#include "sc_scheduler.h"

JDB_DEFINE_TYPENAME(Scheduler, "\033[34mSched\033[m");
Scheduler Scheduler::scheduler;

PUBLIC void
Scheduler::operator delete (void *)
{
  printf("WARNING: tried to delete kernel scheduler object.\n"
         "         The system is now useless\n");
}

PUBLIC inline
Scheduler::Scheduler() : _irq(0)
{
  initial_kobjects.register_obj(this, Initial_kobjects::Scheduler);
}


PRIVATE
L4_msg_tag
Scheduler::sys_run(L4_fpage::Rights, Syscall_frame *f, Utcb const *utcb)
{
  L4_msg_tag tag = f->tag();
  Cpu_number const curr_cpu = current_cpu();

  unsigned long sz = tag.words() * sizeof(Mword);
  if (EXPECT_FALSE(sz < sizeof(L4_sched_param) + sizeof(Mword)))
    return commit_result(-L4_err::EInval);
  sz -= sizeof(Mword); // skip first Mword containing the Opcode

  Ko::Rights rights;
  Thread *thread = Ko::deref<Thread>(&tag, utcb, &rights);
  if (!thread)
    return tag;

  Mword _store[sz / sizeof(Mword)];
  memcpy(_store, &utcb->values[1], sz);

  L4_sched_param const *sched_param = reinterpret_cast<L4_sched_param const *>(_store);

  if (!sched_param->is_legacy())
    if (EXPECT_FALSE(sched_param->length > sz))
      return commit_result(-L4_err::EInval);

  static_assert(sizeof(L4_sched_param_legacy) <= sizeof(L4_sched_param),
                "Adapt above check");

  int ret = Sched_context::check_param(sched_param);
  if (EXPECT_FALSE(ret < 0))
    return commit_result(ret);

  Thread::Migration info;

  Cpu_number const t_cpu = thread->home_cpu();

  if (Cpu::online(t_cpu) && sched_param->cpus.contains(t_cpu))
    info.cpu = t_cpu;
  else if (sched_param->cpus.contains(curr_cpu))
    info.cpu = curr_cpu;
  else
    info.cpu = sched_param->cpus.first(Cpu::present_mask(), Config::max_num_cpus());

  info.sp = sched_param;
  if (0)
    printf("CPU[%u]: run(thread=%lx, cpu=%u (%lx,%u,%u)\n",
           cxx::int_value<Cpu_number>(curr_cpu), thread->dbg_id(),
           cxx::int_value<Cpu_number>(info.cpu),
           utcb->values[2],
           cxx::int_value<Cpu_number>(sched_param->cpus.offset()),
           cxx::int_value<Order>(sched_param->cpus.granularity()));

  // TOMO: ugly :(
  // this is because with the legacy interface, "schedule thread" is also used to set sched parameters.
  // with new interface, userspace should interact with sched_context cap directly.
  if (M_SCHEDULER_DEBUG) printf("SCHEDULER> run_thread() was called!\n");
  if (!thread->sched())
  {
    if (M_SCHEDULER_DEBUG)
    {
      printf("SCHEDULER> trying to run thread %p which has no sched_context attached.\n", thread);
      printf("SCHEDULER> creating a new one...\n");
      //printf("SCHEDULER> RQ has %d entries.\n", SC_Scheduler::rq.current().c);
    }
    // TOMO: set prio directly from sched_param.
    thread->alloc_sched_context();
    if (M_SCHEDULER_DEBUG) printf("SCHEDULER> thread %p got sched_context %p\n", thread, thread->sched());
    // TOMO: in which ready_queue to put here?
    // maybe this is important for MP implementation.
    //SC_Scheduler::deblock(thread->sched());
  }

  thread->migrate(&info);

  //thread->sched()->set(sched_param);
  //if (thread != current())
  //  SC_Scheduler::schedule(false);

  return commit_result(0);
}

PRIVATE
L4_msg_tag
Scheduler::sys_run3(L4_fpage::Rights, Syscall_frame *f, Utcb const *utcb)
{
  L4_msg_tag tag = f->tag();
  Ko::Rights rights;
  L4_snd_item_iter snd_items(utcb, tag.words());

  if (!tag.items())
    return commit_result(-L4_err::EInval);

  Space *const space = ::current()->space();
  if (!space)
    __builtin_unreachable();

  Thread *thread;
  thread = Ko::deref_next<Thread>(&tag, utcb, snd_items, space, &rights);
  if (!thread)
    return tag;

  Sched_context *sc;
  sc = Ko::deref_next<Sched_context>(&tag, utcb, snd_items, space, &rights);
  if (!sc)
    return tag;

  if (0)
  {
    printf("tr: %p\n", thread);
    printf("sc: %p\n", sc);
    printf("tr->sc: %p\n", thread->sched());
    printf("tr->sc.rq: %s\n", thread->sched()->in_ready_queue() ? "y" : "n");
    printf("sc->tr: %p\n", sc->context());
  }

  Sched_context *old_sc = thread->sched();
  if (old_sc)
  {
    // TOMO: WARNING. resource leak here.
    // At the moment, we don't know if old_sc was created manually in the kernel or dynamically from userspace.
    // Therefore, we also don't know whether we should dec_ref() the old_sc,
    // therefore old_sc might not be deleted later and we have a potential resource leak here.
    // To fix this, we would need to adapt the entire userspace and convert libloader/moe/pthread/etc. to dynamic (explicit) sc allocation.
    Ready_queue &rq { Ready_queue::rq.cpu(thread->home_cpu()) };
    rq.switch_sched(old_sc, sc);
    // TOMO: maybe set to idle thread instead? (should be safer...)
    old_sc->set_context(nullptr);
  }

  thread->set_sched(sc);
  sc->set_context(thread);

  // we fabricate a migration info and go the established way.
  Thread::Migration info;

  // sched_contexts are not concerned with core migration, therefore:
  info.cpu = ::current_cpu();

  // extract scheduling information from the given sched_context.
  L4_sched_param_fixed_prio sched_param;
  sched_param.sched_class = L4_sched_param_fixed_prio::Class;
  sched_param.quantum = sc->quantum();
  sched_param.prio = sc->prio();
  info.sp = &sched_param;

  thread->migrate(&info);

  return commit_result(0);
}

PRIVATE
L4_msg_tag
Scheduler::op_sched_idle(L4_cpu_set const &cpus, Cpu_time *time)
{
  Cpu_number const cpu = cpus.first(Cpu::online_mask(), Config::max_num_cpus());
  if (EXPECT_FALSE(cpu == Config::max_num_cpus()))
    return commit_result(-L4_err::EInval);

  *time = Context::kernel_context(cpu)->consumed_time();
  return commit_result(0);
}

PRIVATE
L4_msg_tag
Scheduler::op_sched_info(L4_cpu_set_descr const &s, Mword *m, Mword *max_cpus,
                         Mword *sched_classes)
{
  Mword rm = 0;
  Cpu_number max = Config::max_num_cpus();
  Order granularity = s.granularity();
  Cpu_number const offset = s.offset();

  if (offset >= max)
    return commit_result(-L4_err::ERange);

  if (max > offset + Cpu_number(MWORD_BITS) << granularity)
    max = offset + Cpu_number(MWORD_BITS) << granularity;

  for (Cpu_number i = Cpu_number::first(); i < max - offset; ++i)
    if (Cpu::present_mask().get(i + offset))
      rm |= 1ul << cxx::int_value<Cpu_number>(i >> granularity);

  *m = rm;
  *max_cpus = Config::Max_num_cpus;
  *sched_classes = Sched_context::sched_classes();

  return commit_result(0);
}

PUBLIC inline
Irq_base *
Scheduler::icu_get_irq(unsigned irqnum)
{
  if (irqnum > 0)
    return 0;

  return _irq;
}

PUBLIC inline
L4_msg_tag
Scheduler::op_icu_get_info(Mword *features, Mword *num_irqs, Mword *num_msis)
{
  *features = 0; // supported features (only normal irqs)
  *num_irqs = 1;
  *num_msis = 0;
  return L4_msg_tag(0);
}

PUBLIC
L4_msg_tag
Scheduler::op_icu_bind(unsigned irqnum, Ko::Cap<Irq> const &irq)
{
  if (irqnum > 0)
    return commit_result(-L4_err::EInval);

  if (_irq)
    _irq->unbind();

  if (!Ko::check_rights(irq.rights, Ko::Rights::CW()))
    return commit_result(-L4_err::EPerm);

  Irq_chip_soft::bind(irq.obj, irqnum);
  _irq = irq.obj;
  return commit_result(0);
}

PUBLIC
L4_msg_tag
Scheduler::op_icu_set_mode(Mword pin, Irq_chip::Mode)
{
  if (pin != 0)
    return commit_result(-L4_err::EInval);

  if (_irq)
    _irq->switch_mode(true);
  return commit_result(0);
}

PUBLIC inline
void
Scheduler::trigger_hotplug_event()
{
  if (_irq)
    _irq->hit(0);
}

PUBLIC
L4_msg_tag
Scheduler::kinvoke(L4_obj_ref ref, L4_fpage::Rights rights, Syscall_frame *f,
                   Utcb const *iutcb, Utcb *outcb)
{
  L4_msg_tag tag = f->tag();

  if (tag.proto() == L4_msg_tag::Label_irq)
    return Icu::icu_invoke(ref, rights, f, iutcb, outcb);

  if (!Ko::check_basics(&tag, rights, L4_msg_tag::Label_scheduler))
    return tag;

  switch (iutcb->values[0])
    {
    case Info:       return Msg_sched_info::call(this, tag, iutcb, outcb);
    case Run_thread: return sys_run(rights, f, iutcb);
    case Idle_time:  return Msg_sched_idle::call(this, tag, iutcb, outcb);
    case Run_thread3: return sys_run3(rights, f, iutcb);
    default:         return commit_result(-L4_err::ENosys);
    }
}
