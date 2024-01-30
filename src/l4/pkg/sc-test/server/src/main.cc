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

void *pthread_func(void *arg)
{
  char const *s = static_cast<char const *>(arg);
  for (;;)
  {
    printf("Hello %s!\n", s);
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

  pthread_t thread1;
  pthread_attr_t attr1;
  pthread_attr_init(&attr1);
  attr1.create_flags |= PTHREAD_L4_ATTR_NO_START;
  void *arg1 = static_cast<void *>(const_cast<char *>("World1"));
  if (pthread_create(&thread1, &attr1, pthread_func, arg1))
    L4Re::chksys(-L4_ENOSYS, "pthread create failure");
  pthread_attr_destroy(&attr1);
  L4::Cap<L4::Thread> t1(pthread_l4_cap(thread1));

  pthread_t thread2;
  pthread_attr_t attr2;
  pthread_attr_init(&attr2);
  attr2.create_flags |= PTHREAD_L4_ATTR_NO_START;
  void *arg2 = static_cast<void *>(const_cast<char *>("World2"));
  if (pthread_create(&thread2, &attr2, pthread_func, arg2))
    L4Re::chksys(-L4_ENOSYS, "pthread create failure");
  pthread_attr_destroy(&attr2);
  L4::Cap<L4::Thread> t2(pthread_l4_cap(thread2));

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
  s->set_prio(t1, 255);
  s->set_prio(t2, 255);

  s->attach_sc(t1, sc);
  s->attach_sc(t2, sc);

  l4_sched_param_t sp = l4_sched_param(255, 0);
  s->run_thread(t1, sp);
  s->run_thread(t2, sp);

  for(int i = 0; i < 15; i++)
  {
    printf("%lld\n", l4_kip_clock(l4re_kip()));
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  s->detach_sc(t2, sc);

  for(;;)
  {
    printf("%lld\n", l4_kip_clock(l4re_kip()));
    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  //pthread_join(thread1, nullptr);
  //pthread_join(thread2, nullptr);

  printf("Done\n");

  return 0;
}
