#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/util/cap_alloc>
#include <l4/sys/scheduler>
#include <l4/sys/sched_context>
#include <pthread-l4.h>

#include <cstdio>
#include <chrono>
#include <thread>

void *pthread_func(void *);

void *pthread_func(void *)
{
  for (int i = 0; i < 10; i++)
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
  //L4::Cap<L4::Thread> t = e->main_thread();

  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  attr.create_flags |= PTHREAD_L4_ATTR_NO_START;
  if (pthread_create(&thread, &attr, pthread_func, nullptr))
      L4Re::chksys(-L4_ENOSYS, "pthread create failure");
  pthread_attr_destroy(&attr);
  L4::Cap<L4::Thread> t(pthread_l4_cap(thread));

  L4::Cap<L4::Sched_context> sc;
  sc = L4Re::Util::cap_alloc.alloc<L4::Sched_context>();
  L4Re::chkcap(sc, "sched_context cap alloc");

  l4_msgtag_t r;
  r = f->create(sc);
  L4Re::chksys(r, "sched_context factory create");

  L4Re::chksys(sc->test(), "sched_context test");
  L4Re::chksys(s->run_thread3(t, sc), "scheduler sys_run3");
  L4Re::chksys(sc->test(), "sched_context test");

  printf("Done\n");

  pthread_join(thread, nullptr);

  return 0;
}
