#include <l4/cxx/utils>
#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/util/cap_alloc>
#include <l4/sys/scheduler>
#include <l4/sys/sched_constraint>
#include <l4/util/util.h>

#include <pthread-l4.h>
#include <cassert>

#include <unistd.h>

using L4Re::chkcap;
using L4Re::chksys;

void *workload(void *arg);

void *workload(void *arg)
{
  unsigned long cpu = (unsigned long)arg;
  while (true)
  {
    printf("work%lu;\n", cpu);
    sleep(1);
  }
  return nullptr;
}

int main(void)
{
  printf("benchmark prepare\n");
  L4Re::Env const *e = L4Re::Env::env();
  L4::Cap<L4::Factory> f = e->factory();
  L4::Cap<L4::Scheduler> s = e->scheduler();

  pthread_t pt_local, pt_remote;
  pthread_attr_t a;
  pthread_attr_init(&a);
  a.create_flags |= PTHREAD_L4_ATTR_NO_START;
  if (pthread_create(&pt_local, &a, workload, (void *)0))
    chksys(-L4_ENOSYS, "pthread_create");
  if (pthread_create(&pt_remote, &a, workload, (void *)1))
    chksys(-L4_ENOSYS, "pthread_create");
  pthread_attr_destroy(&a);
  L4::Cap<L4::Thread> t_local(pthread_l4_cap(pt_local));
  L4::Cap<L4::Thread> t_remote(pthread_l4_cap(pt_remote));

  L4::Cap<L4::Cond_sc> sc;
  sc = L4Re::Util::cap_alloc.alloc<L4::Cond_sc>();
  chkcap(sc, "sched_constraint cap alloc");
  l4_msgtag_t r = f->create(sc) << l4_umword_t(L4_SCHED_CONSTRAINT_TYPE_COND);
  chksys(r, "sched_constraint factory create");
  s->attach_sc(t_local, sc);
  s->attach_sc(t_remote, sc);

  L4::Cap<L4::Thread> t_self(pthread_l4_cap(pthread_self()));
  s->set_prio(t_self, 254);

  printf("benchmark start\n");

  l4_sched_param_t sp = l4_sched_param(255);
  sp.affinity.set(0, 0);
  sp.affinity.map = 1;
  s->set_prio(t_local, 255);
  s->run_thread(t_local, sp);
  sp.affinity.map = 2;
  s->set_prio(t_remote, 255);
  s->run_thread(t_remote, sp);

  sleep(10);
  printf("flipping cond sc\n");
  sc->flip();
  sleep(10);
  printf("flipping cond sc\n");
  sc->flip();
  sleep(10);
  printf("flipping cond sc\n");
  sc->flip();

  pthread_join(pt_local, nullptr);
  pthread_join(pt_remote, nullptr);

  printf("benchmark done\n");

  return 0;
}
