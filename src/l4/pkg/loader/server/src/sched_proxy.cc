/*
 * (c) 2008-2009 Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */
#include "sched_proxy.h"
#include "global.h"
#include "debug.h"
#include "obj_reg.h"

#include <algorithm>
#include <l4/re/env>
#include <l4/sys/scheduler>

//#include <cstdio>

static
l4_sched_cpu_set_t
blow_up(l4_sched_cpu_set_t const &src, unsigned char gran)
{
  l4_sched_cpu_set_t n;
  gran &= sizeof(l4_umword_t) * 8 - 1;
  unsigned char og = src.granularity() & (sizeof(l4_umword_t) * 8 - 1);
  n.set(gran, src.offset() & (~0UL << og));
  n.map = 0;
  for (unsigned i = 0; i < sizeof(l4_umword_t) * 8; ++i)
    if (src.map & (1UL << (i >> (og - gran))))
      n.map |= 1UL << i;

  return n;
}

static
l4_sched_cpu_set_t operator & (l4_sched_cpu_set_t const &a, l4_sched_cpu_set_t const &b)
{
  l4_sched_cpu_set_t _a, _b;
  unsigned char const ga = a.granularity() & (sizeof(l4_umword_t) * 8 - 1);
  unsigned char const gb = b.granularity() & (sizeof(l4_umword_t) * 8 - 1);
  if (ga < gb)
    {
      _b = blow_up(b, ga);
      _a = a;
    }
  else if (ga == gb)
    {
      _a = a;
      _b = b;
    }
  else
    {
      _a = blow_up(a, gb);
      _b = b;
    }

  long ofs_dif = _a.offset() - _b.offset();
  long unsigned abs_ofs_dif;
  if (ofs_dif < 0)
    abs_ofs_dif = -ofs_dif;
  else
    abs_ofs_dif = ofs_dif;

  if (abs_ofs_dif >= sizeof(l4_umword_t) * 8)
    return l4_sched_cpu_set(0,0,0);

  if (ofs_dif < 0)
    {
      _b.map &= (_a.map >> abs_ofs_dif);
      return _b;
    }
  else
    {
      _a.map &= (_b.map >> abs_ofs_dif);
      return _a;
    }
}

Sched_proxy::List Sched_proxy::_list;

Sched_proxy::Sched_proxy() : L4kproxy::Scheduler_svr(this),
  _real_cpus(l4_sched_cpu_set(0,0,0)), _cpu_mask(_real_cpus),
  _max_cpus(0), _sched_classes(0),
  _prio_offset(0), _prio_limit(0)
{
  rescan_cpus_and_classes();
  _list.push_front(this);
}

void
Sched_proxy::rescan_cpus_and_classes()
{
  l4_sched_cpu_set_t c;
  l4_umword_t max = 0;
  c.map = 0;
  c.gran_offset = 0;
  l4_umword_t sc = 0;

  int e = l4_error(L4Re::Env::env()->scheduler()->info(&max, &c, &sc));

  if (e < 0)
    return;

  _max_cpus = std::min<unsigned>(sizeof(l4_umword_t) * 8, max);
  _real_cpus = c;

  _cpus = _real_cpus & _cpu_mask;

  _sched_classes = sc;
}

Sched_proxy::~Sched_proxy()
{
  _list.remove(this);
}

int
Sched_proxy::info(l4_umword_t *cpu_max, l4_sched_cpu_set_t *cpus,
                  l4_umword_t *sched_classes)
{
  *cpu_max = _max_cpus;
  unsigned char g = cpus->granularity() & (sizeof(l4_umword_t) * 8 - 1);
  l4_umword_t offs = cpus->offset() & (~0UL << g);
  if (offs >= _max_cpus)
    return -L4_ERANGE;

  cpus->map = 0;
  unsigned b = 0;
  for (unsigned i = offs; i < _max_cpus && b < sizeof(l4_umword_t) * 8;)
    {
      if (_cpus.map & (1UL << i))
	cpus->map |= 1UL << b;

      ++i;

      if (!(i & ~(~0UL << g)))
	++b;
    }

  if (sched_classes)
    *sched_classes = _sched_classes;
  return L4_EOK;
}


int
Sched_proxy::run_thread(L4::Cap<L4::Thread> thread, l4_sched_param_t const &sp)
{
  l4_sched_param_t s = sp;
  s.prio = std::min(sp.prio + _prio_offset, (l4_umword_t)_prio_limit);
  s.affinity = sp.affinity & _cpus;
#if 0
  printf("loader[%p] run_thread: o=%u scheduler affinity = %lx sp.m=%lx sp.o=%u sp.g=%u\n",
      this, _cpus.offset, _cpus.map, sp.affinity.map, (unsigned)sp.affinity.offset, (unsigned)sp.affinity.granularity);
  printf("loader[%p]                                      sp.m=%lx sp.o=%u sp.g=%u\n",
      this, s.affinity.map, (unsigned)s.affinity.offset, (unsigned)s.affinity.granularity);
#endif
  return l4_error(L4Re::Env::env()->scheduler()->run_thread(thread, s));
}

int
Sched_proxy::run_thread3(L4::Cap<L4::Thread> thread, L4::Cap<L4::Sched_context> sc)
{
  return l4_error(L4Re::Env::env()->scheduler()->run_thread3(thread, sc));
}

int
Sched_proxy::idle_time(l4_sched_cpu_set_t const &, l4_kernel_clock_t &)
{ return -L4_ENOSYS; }


L4::Cap<L4::Thread>
Sched_proxy::received_thread(L4::Ipc::Snd_fpage const &fp)
{
  if (!fp.cap_received())
    return L4::Cap<L4::Thread>::Invalid;

  return server_iface()->rcv_cap<L4::Thread>(0);
}

L4::Cap<L4::Sched_context>
Sched_proxy::received_sc(L4::Ipc::Snd_fpage const &fp)
{
  if (!fp.cap_received())
    return L4::Cap<L4::Sched_context>::Invalid;

  return server_iface()->rcv_cap<L4::Sched_context>(1);
}

void
Sched_proxy::restrict_cpus(l4_umword_t cpus)
{
  _cpu_mask = l4_sched_cpu_set(0, 0, cpus);
  _cpus = _real_cpus & _cpu_mask;
}


class Cpu_hotplug_server : public L4::Irqep_t<Cpu_hotplug_server>
{
public:
  void handle_irq()
  {
    typedef Sched_proxy::List List;
    for (List::Const_iterator i = Sched_proxy::_list.begin();
         i != Sched_proxy::_list.end();
         ++i)
      {
        i->rescan_cpus_and_classes();
        i->hotplug_event();
      }
  }

  Cpu_hotplug_server()
  {
    L4::Cap<L4::Irq> irq = Gate_alloc::registry.register_irq_obj(this);
    if (!irq)
      {
        Err(Err::Fatal).printf("Could not allocate IRQ for CPU hotplug\n");
        return;
      }

    if (l4_error(L4Re::Env::env()->scheduler()->bind(0, irq)) < 0)
      {
        Err(Err::Fatal).printf("Could not bind CPU hotplug IRQ to scheduler\n");
        return;
      }
  }
};

static Cpu_hotplug_server _cpu_hotplug_server;



