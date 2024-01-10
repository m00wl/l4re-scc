#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/util/cap_alloc>
#include <l4/sys/sched_constraint>
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

  //L4Re::Env const *e = L4Re::Env::env();
  //L4::Cap<L4::Factory> f = e->factory();

  //L4::Cap<L4::Budget_sc> s;
  //s = L4Re::Util::cap_alloc.alloc<L4::Budget_sc>();
  //L4Re::chkcap(s, "sched_constraint cap alloc");

  //l4_msgtag_t r;
  //r = (f->create(s) << l4_mword_t(L4::Sched_constraint::Type::Budget_sc));
  //L4Re::chksys(r, "sched_constraint factory create");

  //s->test();
  //s->print();

  L4Re::Env const *e = L4Re::Env::env();
  L4::Cap<L4::Factory> f = e->factory();
  //L4::Cap<L4::Scheduler> s = e->scheduler();

  pthread_t thread;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  attr.create_flags |= PTHREAD_L4_ATTR_NO_START;
  if (pthread_create(&thread, &attr, pthread_func, nullptr))
    L4Re::chksys(-L4_ENOSYS, "pthread create failure");
  pthread_attr_destroy(&attr);
  L4::Cap<L4::Thread> t(pthread_l4_cap(thread));

  t->clear_scs();

  L4::Cap<L4::Budget_sc> s;
  s = L4Re::Util::cap_alloc.alloc<L4::Budget_sc>();
  L4Re::chkcap(s, "sched_constraint cap alloc");

  l4_msgtag_t r;
  r = (f->create(s) << l4_mword_t(L4::Sched_constraint::Type::Budget_sc));
  L4Re::chksys(r, "sched_constraint factory create");

  t->attach_sc(s);

  printf("Done\n");

  return 0;
}
