#include <l4/cxx/utils>
#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/re/util/cap_alloc>
#include <l4/sys/scheduler>
#include <l4/util/util.h>

#include <pthread-l4.h>

#include <cstdio>
#include <chrono>
#include <random>

using L4Re::chkcap;
using L4Re::chksys;
using cxx::access_once;

static constexpr auto N_THREADS = 3;
static constexpr auto N_READS = 16777216;
static constexpr auto MEM_SIZE = 1073741824; // 1 GiB
static constexpr auto CACHE_LINE_SIZE = 64;

std::default_random_engine dre(0x533D);
std::uniform_int_distribution<l4_addr_t> uid(0, MEM_SIZE - 8);
l4_addr_t ds_start = 0;

unsigned long get_current_time_in_ms(void);
void*benchmark_core(void*);

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

void *benchmark_core(void *arg)
{
  unsigned long n = reinterpret_cast<unsigned long>(arg);
  
  l4_uint64_t res = 0;
  
  unsigned long start = get_current_time_in_ms();

  //auto loc = ds_start;

  //for (int i = 0; i < N_READS; i++)
  volatile int endless = 1;
  while (endless)
  {
    //l4_sleep_forever();
    auto loc = ds_start + uid(dre);
    res += access_once(reinterpret_cast<l4_uint64_t *>(loc));
    //loc += 64;
  }

  unsigned long end = get_current_time_in_ms();
  printf("thread%lu> time: %lu, res: %llu\n", n, end - start, res);

  return nullptr;
}


int main(void)
{
  printf("Preparing...\n");
  L4Re::Env const *e = L4Re::Env::env();
  L4::Cap<L4::Scheduler> s = e->scheduler();
  L4::Cap<L4Re::Dataspace> ds;

  ds = chkcap(L4Re::Util::cap_alloc.alloc<L4Re::Dataspace>(), "ds cap alloc");

  chksys(e->mem_alloc()->alloc(MEM_SIZE, ds), "ds mem alloc");

  L4Re::Rm::Flags rm_flags = L4Re::Rm::F::R | L4Re::Rm::F::Search_addr
                             | L4Re::Rm::F::Eager_map;
  chksys(e->rm()->attach(&ds_start, MEM_SIZE, rm_flags, ds), "ds AS attach");

  L4Re::Dataspace::Flags ds_flags = L4Re::Dataspace::F::R;
  l4_addr_t ds_end = ds_start + MEM_SIZE;
  chksys(ds->map_region(0, ds_flags, ds_start, ds_end), "ds mem map");

  pthread_t threads[N_THREADS];

  for (unsigned i = 0; i < N_THREADS; i++)
  {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    attr.create_flags |= PTHREAD_L4_ATTR_NO_START;
    void *arg = reinterpret_cast<void *>(i);
    if (pthread_create(&threads[i], &attr, benchmark_core, arg))
      L4Re::chksys(-L4_ENOSYS, "pthread create failure");
    pthread_attr_destroy(&attr);
  }

  printf("Start Pointer Chasing\n");

  for (int i = N_THREADS - 1; i >= 0; i--)
  {
    l4_sched_param_t sp = l4_sched_param(255); 
    sp.affinity.set(0, i+1);
    sp.affinity.map = 1;
    L4::Cap<L4::Thread> t(pthread_l4_cap(threads[i]));
    s->set_prio(t, 255);
    s->run_thread(t, sp);
  }

  for (int i = N_THREADS - 1; i >= 0; i--)
  { pthread_join(threads[i], nullptr); }

  printf("benchmark done\n");

  l4_sleep_forever();

  return 0;
}
