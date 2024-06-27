#include <l4/cxx/utils>
#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/util/cap_alloc>
#include <l4/sys/scheduler>
#include <l4/sys/sched_constraint>
#include <l4/util/util.h>

#include <pthread-l4.h>

#include <cstdio>
#include <chrono>

using L4Re::chkcap;
using L4Re::chksys;

unsigned long get_current_time_in_ms(void);
void *cpu_hog(void *);
void *calc_pi(void *);

unsigned long get_current_time_in_ms(void)
{
  typedef std::chrono::high_resolution_clock hrclock;
  typedef std::chrono::time_point<std::chrono::high_resolution_clock> tp_t;
  typedef std::chrono::milliseconds to_ms;
  unsigned long ms;

  tp_t now = hrclock::now();
  ms = std::chrono::duration_cast<to_ms>(now.time_since_epoch()).count();

  return ms;
}

void *calc_pi(void *)
{
  enum
  {
    Scale = 10000,
    Init  = 2000,

    Digits = 15000,
    Print = 0,
  };

  unsigned carry = 0;
  //unsigned arr[Digits + 1];
  unsigned *arr = static_cast<unsigned *>(malloc((Digits + 1) * sizeof(unsigned)));

  printf("start calculating pi\n");

  for (int i = 0; i <= Digits; ++i)
    arr[i] = Init;

  unsigned long start = get_current_time_in_ms();

  for (int i = Digits; i > 0; i -= 14)
    {
      int sum = 0;
      for (int j = i; j > 0; --j)
        {
          sum = sum * j + Scale * arr[j];
          arr[j] = sum % (j * 2 - 1);
          sum /= j * 2 - 1;
        }

      if (Print)
        {
          printf("%04d", carry + sum / Scale);
          carry = sum % Scale;
        }
      else
        asm volatile("" : : "r" (sum));
    }

  unsigned long end = get_current_time_in_ms();
  unsigned long duration = end - start;
  printf("%d digits, %lu ms, %.4f digits/s\n", Digits, duration,
         double{Digits} / duration * 1000);

  return nullptr;
}

void *cpu_hog(void *)
{
  volatile int n = 1;
  while (n)
  { asm volatile("" : : "r" (n)); }
  return nullptr;
}

int main(void)
{
  calc_pi(nullptr);
  //printf("benchmark prepare\n");
  //L4Re::Env const *e = L4Re::Env::env();
  //L4::Cap<L4::Factory> f = e->factory();
  //L4::Cap<L4::Scheduler> s = e->scheduler();

  //pthread_t thread_prio;
  //pthread_attr_t attr_prio;
  //pthread_attr_init(&attr_prio);
  //attr_prio.create_flags |= PTHREAD_L4_ATTR_NO_START;
  //if (pthread_create(&thread_prio, &attr_prio, cpu_hog, nullptr))
  //  L4Re::chksys(-L4_ENOSYS, "pthread create failure");
  //pthread_attr_destroy(&attr_prio);
  //L4::Cap<L4::Thread> t_prio(pthread_l4_cap(thread_prio));
  //s->detach_sc(t_prio, L4::Cap<L4::Budget_sc>());
  //L4::Cap<L4::Budget_sc> sc;
  //sc = L4Re::Util::cap_alloc.alloc<L4::Budget_sc>();
  //chkcap(sc, "sched_constraint cap alloc");
  //l4_msgtag_t r = f->create(sc) << l4_umword_t(L4_SCHED_CONSTRAINT_TYPE_BUDGET);
  //chksys(r, "sched_constraint factory create");
  //s->attach_sc(t_prio, sc);

  //pthread_t thread_be;
  //pthread_attr_t attr_be;
  //pthread_attr_init(&attr_be);
  //attr_be.create_flags |= PTHREAD_L4_ATTR_NO_START;
  //if (pthread_create(&thread_be, &attr_be, calc_pi, nullptr))
  //  L4Re::chksys(-L4_ENOSYS, "pthread create failure");
  //pthread_attr_destroy(&attr_be);
  //L4::Cap<L4::Thread> t_be(pthread_l4_cap(thread_be));

  //printf("benchmark start\n");

  //l4_sched_param_t sp_prio = l4_sched_param(255); 
  //sp_prio.affinity.set(0, 1);
  //sp_prio.affinity.map = 1;
  //s->set_prio(t_prio, 255);
  //s->run_thread(t_prio, sp_prio);

  //l4_sched_param_t sp_be = l4_sched_param(254); 
  //sp_be.affinity.set(0, 1);
  //sp_be.affinity.map = 1;
  //s->set_prio(t_be, 254);
  //s->run_thread(t_be, sp_be);

  //l4_sleep_forever();

  //pthread_join(thread_prio, nullptr);
  //pthread_join(thread_be, nullptr);

  //printf("benchmark done\n");

  return 0;
}
