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
#include <unistd.h>

using L4Re::chkcap;
using L4Re::chksys;

unsigned long get_current_time_in_ms(void);
void *cpu_hog(void *);
void *calc_pi(void *);
void *workload(void *);

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

void *workload(void *)
{
  while (true)
  {
    printf("work;\n");
    sleep(1);
  }
  return nullptr;
}

int main(void)
{
  calc_pi(nullptr);
  //printf("benchmark prepare\n");
  //L4Re::Env const *e = L4Re::Env::env();
  //L4::Cap<L4::Factory> f = e->factory();
  //L4::Cap<L4::Scheduler> s = e->scheduler();

  //pthread_t pt;
  //pthread_attr_t a;
  //pthread_attr_init(&a);
  //a.create_flags |= PTHREAD_L4_ATTR_NO_START;
  //if (pthread_create(&pt, &a, workload, nullptr))
  //  chksys(-L4_ENOSYS, "pthread_create");
  //pthread_attr_destroy(&a);
  //L4::Cap<L4::Thread> t(pthread_l4_cap(pt));

  //L4::Cap<L4::Cond_sc> sc;
  //sc = L4Re::Util::cap_alloc.alloc<L4::Cond_sc>();
  //chkcap(sc, "sched_constraint cap alloc");
  //l4_msgtag_t r = f->create(sc) << l4_umword_t(L4_SCHED_CONSTRAINT_TYPE_COND);
  //chksys(r, "sched_constraint factory create");
  //s->attach_sc(t, sc);

  //L4::Cap<L4::Thread> tself(pthread_l4_cap(pthread_self()));
  //s->set_prio(tself, 254);

  //printf("benchmark start\n");

  //l4_sched_param_t sp = l4_sched_param(255);
  //sp.affinity.set(0, 1);
  //sp.affinity.map = 1;
  //s->set_prio(t, 255);
  //s->run_thread(t, sp);

  //// TODO: play with Cond_sc here.
  //// TODO: ausprobieren
  //// TODO: auslagern in eigenes Paket
  //// TODO: zusätzliche threads auf anderen cores starten
  //sleep(10);
  //printf("flipping cond sc\n");
  //sc->flip();
  //sleep(10);
  //printf("flipping cond sc\n");
  //sc->flip();
  //sleep(10);
  //printf("flipping cond sc\n");
  //sc->flip();
  //sleep(10);

  //pthread_join(t, nullptr);

  //printf("benchmark done\n");

  return 0;
}
