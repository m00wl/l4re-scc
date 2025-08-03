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
    Info          = 0,
    Run_thread    = 1,
    Idle_time     = 2,
    Set_prio      = 3,
    Attach_sc     = 4,
    Detach_sc     = 5,
    Set_global_sc = 6,
  };

  static Scheduler scheduler;
private:
  Irq_base *_irq;
  Sched_constraint *_global_sc;

  L4_RPC(Info,      sched_info, (L4_cpu_set_descr set, Mword *rm,
                                 Mword *max_cpus, Mword *sched_classes));
  L4_RPC(Idle_time, sched_idle, (L4_cpu_set cpus, Cpu_time *time));

  void sys_run_call_in(Thread *);
};

// ----------------------------------------------------------------------------
IMPLEMENTATION:

#include "thread_object.h"
#include "l4_buf_iter.h"
#include "l4_types.h"
#include "entry_frame.h"
#include "mbwp.h"

JDB_DEFINE_TYPENAME(Scheduler, "\033[34mSched\033[m");
Scheduler Scheduler::scheduler;

PUBLIC void
Scheduler::operator delete (void *)
{
  printf("WARNING: tried to delete kernel scheduler object.\n"
         "         The system is now useless\n");
}

PUBLIC inline
Scheduler::Scheduler() : _irq(0), _global_sc(0)
{
  initial_kobjects.register_obj(this, Initial_kobjects::Scheduler);
}

IMPLEMENT_DEFAULT inline
void
Scheduler::sys_run_call_in(Thread *)
{}

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

  //printf("\033[1;33mSCHEDULER> run_thread C[%p] (Warning: Prio/Timeslice in L4_sched_param ignored, use SC API instead)\033[0m\n", thread);
  printf("\033[1;33mSCHEDULER> run_thread C[%p] on cpu %d\033[0m\n", thread, cxx::int_value<Cpu_number>(info.cpu));
  if (!thread->sched()->is_constrained())
  {
    printf("thread: %p\n", thread);
    panic("sched_context has no sched_constraints (scheduler)");
    if (M_SCHEDULER_DEBUG)
      printf("SCHEDULER> trying to run thread %p whose sched_context has no sched_constraints attached.\n", thread);
    thread->alloc_sched_constraints();
  }

  sys_run_call_in(thread);
  //Sched_constraint *mbwp_sc = Mbwp::sc.cpu(info.cpu);
  //if (!thread->sched()->contains(mbwp_sc))
  //{
  //  thread->sched()->attach(mbwp_sc);
  //  printf("attached mbwp_sc to T[%p] on CPU %d\n", thread, cxx::int_value<Cpu_number>(info.cpu));
  //}

  // TOMO: what happens if attach_sc fails?
  if (_global_sc)
    thread->sched()->attach(_global_sc);

  thread->migrate(&info);

  return commit_result(0);
}

PRIVATE
L4_msg_tag
Scheduler::sys_set_prio(Syscall_frame *f, Utcb const *utcb)
{
  L4_msg_tag tag { f->tag() };
  Ko::Rights rights;

  Thread *thread { Ko::deref<Thread>(&tag, utcb, &rights) };

  if (!thread)
    return tag;

  Unsigned8 prio { static_cast<Unsigned8>(utcb->values[1]) };

  thread->change_prio_to(prio);

  return commit_result(0);
}

PRIVATE
L4_msg_tag
Scheduler::sys_attach_sc(Syscall_frame *f, Utcb const *utcb)
{
  L4_msg_tag tag = f->tag();
  Ko::Rights rights;

  if (tag.items() != 2)
    return commit_result(-L4_err::EInval);

  L4_snd_item_iter snd_items(utcb, tag.words());

  Space *const space = ::current()->space();
  if (!space)
    __builtin_unreachable();

  Thread *thread;
  thread = Ko::deref_next<Thread>(&tag, utcb, snd_items, space, &rights);
  if (!thread)
    return tag;

  Sched_constraint *sc;
  sc = Ko::deref_next<Sched_constraint>(&tag, utcb, snd_items, space, &rights);
  if (!sc)
    return tag;

  if (!thread->sched()->attach(sc))
    return commit_result(-L4_err::ENomem);
  thread->sched()->print();

  return commit_result(0);
}

PRIVATE
L4_msg_tag
Scheduler::sys_detach_sc(Syscall_frame *f, Utcb const *utcb)
{
  L4_msg_tag tag = f->tag();
  Ko::Rights rights;
  L4_snd_item_iter snd_items(utcb, tag.words());

  if (tag.items() != 2)
    return commit_result(-L4_err::EInval);

  Space *const space = ::current()->space();
  if (!space)
    __builtin_unreachable();

  Thread *thread;
  thread = Ko::deref_next<Thread>(&tag, utcb, snd_items, space, &rights);
  if (!thread)
    return tag;

  Sched_constraint *sc;
  sc = Ko::deref_next<Sched_constraint>(&tag, utcb, snd_items, space, &rights);
  if (!sc)
    return tag;

  if (!thread->sched()->detach(sc))
    return commit_result(-L4_err::ENoent);
  //thread->sched()->detach_all();
  thread->sched()->print();

  return commit_result(0);
}

PRIVATE
L4_msg_tag
Scheduler::sys_set_global_sc(Syscall_frame *f, Utcb const *utcb)
{
  L4_msg_tag tag { f->tag() };
  Ko::Rights rights;

  Sched_constraint *sc { Ko::deref<Sched_constraint>(&tag, utcb, &rights) };

  if (!sc)
    return tag;

  _global_sc = sc;

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
    case Info:
      return Msg_sched_info::call(this, tag, iutcb, outcb);
    case Run_thread:
      return sys_run(rights, f, iutcb);
    case Idle_time:
      return Msg_sched_idle::call(this, tag, iutcb, outcb);
    case Set_prio:
      return sys_set_prio(f, iutcb);
    case Attach_sc:
      return sys_attach_sc(f, iutcb);
    case Detach_sc:
      return sys_detach_sc(f, iutcb);
    case Set_global_sc:
      return sys_set_global_sc(f, iutcb);
    default:
      return commit_result(-L4_err::ENosys);
    }
}

// ----------------------------------------------------------------------------
IMPLEMENTATION [mbwp]:

IMPLEMENT_OVERRIDE inline
void
Scheduler::sys_run_call_in(Thread *)
{
  //Sched_constraint *mbwp_sc = Mbwp::sc.cpu(info.cpu);
  //if (!t->sched()->contains(mbwp_sc))
  //  t->sched()->attach(mbwp_sc);
}

