#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/util/cap_alloc>
#include <l4/sys/sched_constraint>

#include <cstdio>

int main(void)
{
  printf("Start\n");

  L4Re::Env const *e = L4Re::Env::env();
  L4::Cap<L4::Factory> f = e->factory();

  L4::Cap<L4::Budget_sc> s;
  s = L4Re::Util::cap_alloc.alloc<L4::Budget_sc>();
  L4Re::chkcap(s, "sched_constraint cap alloc");

  l4_msgtag_t r;
  r = (f->create(s) << l4_mword_t(L4::Sched_constraint::Type::Budget_sc));
  L4Re::chksys(r, "sched_constraint factory create");

  s->test();
  s->print();

  printf("Done\n");

  return 0;
}
