#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/util/cap_alloc>
#include <l4/sys/scheduler>
#include <l4/sys/sched_constraint>
#include <l4/sys/kip>
#include <pthread-l4.h>

#include <cstdio>
#include <chrono>
#include <thread>

void *pthread_func(void *);

void *pthread_func(void *)
{
  for (;;)
  {
    printf("Hello World!\n");
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  return nullptr;
}

int main(void)
{
  printf("Start\n");

  L4Re::Env const *e = L4Re::Env::env();
  L4::Cap<L4::Factory> f = e->factory();
  L4::Cap<L4::Scheduler> s = e->scheduler();

  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  attr.create_flags |= PTHREAD_L4_ATTR_NO_START;
  if (pthread_create(&thread, &attr, pthread_func, nullptr))
    L4Re::chksys(-L4_ENOSYS, "pthread create failure");
  pthread_attr_destroy(&attr);
  L4::Cap<L4::Thread> t(pthread_l4_cap(thread));

  L4::Cap<L4::Timer_window_sc> sc;
  sc = L4Re::Util::cap_alloc.alloc<L4::Timer_window_sc>();
  L4Re::chkcap(sc, "sched_constraint cap alloc");

  auto cs = f->create(sc);
  cs << l4_umword_t(L4::Sched_constraint::Type::Timer_window_sc);
  cs << l4_umword_t(l4_kip_clock(l4re_kip()) + 5000000);
  cs << l4_umword_t(5000000);
  l4_msgtag_t r = cs;
  L4Re::chksys(r, "sched_constraint factory create");

  s->set_prio(e->main_thread(), 254);
  s->set_prio(t, 255);

  s->attach_sc(t, sc);

  l4_sched_param_t sp = l4_sched_param(255, 0);
  s->run_thread(t, sp);

  for(;;)
  {
    printf("%lld\n", l4_kip_clock(l4re_kip()));
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }
  //pthread_join(thread, nullptr);

  printf("Done\n");

  return 0;
}
