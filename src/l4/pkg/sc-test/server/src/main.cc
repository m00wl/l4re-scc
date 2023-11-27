#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/util/cap_alloc>
#include <l4/sys/sched_context>

#include <cstdio>

int main(void)
{
  printf("Start\n");

  L4Re::Env const *e = L4Re::Env::env();
  L4::Cap<L4::Factory> f = e->factory();

  L4::Cap<L4::Sched_context> s;
  s = L4Re::Util::cap_alloc.alloc<L4::Sched_context>();
  L4Re::chkcap(s, "sched_context cap alloc");

  l4_msgtag_t r;
  r = f->create(s);
  L4Re::chksys(r, "sched_context factory create");

  s->test();

  printf("Done\n");

  return 0;
}
