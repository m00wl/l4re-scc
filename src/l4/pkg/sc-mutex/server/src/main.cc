//#include <l4/cxx/utils>
//#include <l4/re/env>
//#include <l4/re/error_helper>
//#include <l4/re/util/cap_alloc>
//#include <l4/sys/scheduler>
//#include <l4/sys/sched_constraint>
//#include <l4/sys/l4int.h>
//#include <l4/util/util.h>
//
//#include <pthread-l4.h>
//#include <cassert>
//#include <chrono>
//
//#include <unistd.h>
//
//using L4Re::chkcap;
//using L4Re::chksys;
//
//static inline l4_uint64_t read_pmccntr_el0(void)
//{
//    uint64_t val;
//    asm volatile("isb\n\t"
//                 "mrs %0, pmccntr_el0\n\t"
//                 "isb\n\t"
//                 : "=r"(val));
//    return val;
//}
//
//void *workload(void *arg);
//
//void *workload(void *arg)
//{
//  (void)arg;
//  //unsigned long cpu = (unsigned long)arg;
//  //using clock = std::chrono::high_resolution_clock;
//
//  volatile l4_uint64_t i = 0;
//  while (true)
//  {
//    i++;
//    //printf("work%lu.1;\n", cpu);
//    //auto start = clock::now();
//    //auto end = start + std::chrono::duration<double>(1.0);
//    //while (clock::now() < end) {};
//    //printf("work%lu.2;\n", cpu);
//    //printf("work%lu\n", cpu);
//    //sleep(1);
//  }
//  return nullptr;
//}
//
//int main(void)
//{
//  printf("benchmark prepare\n");
//  L4Re::Env const *e = L4Re::Env::env();
//  L4::Cap<L4::Factory> f = e->factory();
//  L4::Cap<L4::Scheduler> s = e->scheduler();
//
//  //pthread_t pt_local;
//  pthread_t pt_remote1;
//  pthread_t pt_remote2;
//  pthread_t pt_remote3;
//  pthread_attr_t a;
//  pthread_attr_init(&a);
//  a.create_flags |= PTHREAD_L4_ATTR_NO_START;
//  //if (pthread_create(&pt_local, &a, workload, (void *)0))
//  //  chksys(-L4_ENOSYS, "pthread_create");
//  if (pthread_create(&pt_remote1, &a, workload, (void *)1))
//    chksys(-L4_ENOSYS, "pthread_create");
//  if (pthread_create(&pt_remote2, &a, workload, (void *)2))
//    chksys(-L4_ENOSYS, "pthread_create");
//  if (pthread_create(&pt_remote3, &a, workload, (void *)3))
//    chksys(-L4_ENOSYS, "pthread_create");
//  pthread_attr_destroy(&a);
//  //L4::Cap<L4::Thread> t_local(pthread_l4_cap(pt_local));
//  L4::Cap<L4::Thread> t_remote1(pthread_l4_cap(pt_remote1));
//  L4::Cap<L4::Thread> t_remote2(pthread_l4_cap(pt_remote2));
//  L4::Cap<L4::Thread> t_remote3(pthread_l4_cap(pt_remote3));
//
//  L4::Cap<L4::Cond_sc> sc;
//  sc = L4Re::Util::cap_alloc.alloc<L4::Cond_sc>();
//  chkcap(sc, "sched_constraint cap alloc");
//  l4_msgtag_t r = f->create(sc) << l4_umword_t(L4_SCHED_CONSTRAINT_TYPE_COND);
//  chksys(r, "sched_constraint factory create");
//  //s->set_cpus_sc(sc, l4_sched_cpu_set(0, 0, 1)); // TODO: IMPORTANT!!!
//  //s->set_cpus_sc(sc, l4_sched_cpu_set(0, 0, 3));
//  //s->set_cpus_sc(sc, l4_sched_cpu_set(0, 0, 7));
//  s->set_cpus_sc(sc, l4_sched_cpu_set(0, 0, 15));
//  //s->attach_sc(t_local, sc);
//  s->attach_sc(t_remote1, sc);
//  s->attach_sc(t_remote2, sc);
//  s->attach_sc(t_remote3, sc);
//
//  L4::Cap<L4::Thread> t_self(pthread_l4_cap(pthread_self()));
//  s->set_prio(t_self, 255);
//
//  printf("benchmark start\n");
//
//  sc->flip();
//
//  l4_sched_param_t sp = l4_sched_param(254);
//  sp.affinity.set(0, 0);
//  sp.affinity.map = 8;
//  s->set_prio(t_remote3, 254);
//  s->run_thread(t_remote3, sp);
//  sp.affinity.map = 4;
//  s->set_prio(t_remote2, 254);
//  s->run_thread(t_remote2, sp);
//  sp.affinity.map = 2;
//  s->set_prio(t_remote1, 254);
//  s->run_thread(t_remote1, sp);
//  //sp.affinity.map = 1;
//  //s->set_prio(t_local, 254);
//  //s->run_thread(t_local, sp);
//
//  printf("benchmark start measurements\n");
//
//  for (int i = 0; i < 10; i++)
//  {
//    sleep(1);
//
//    l4_uint64_t start = read_pmccntr_el0();
//    sc->flip();
//    l4_uint64_t end = read_pmccntr_el0();
//    l4_uint64_t delta = end - start;
//    printf("wake_up_everyone: start: %llu, end: %llu, delta: %llu\n", start, end, delta);
//    sc->flip();
//  }
//
//  //for (int i = 0; i < 10; i++)
//  //{
//  //  //sleep(1);
//  //  //printf("flipping cond sc\n");
//  //  l4_uint64_t start = read_pmccntr_el0();
//  //  sc->flip();
//  //  l4_uint64_t end = read_pmccntr_el0();
//  //  l4_uint64_t delta = end - start;
//  //  printf("stop_everyone: start: %llu, end: %llu, delta: %llu\n", start, end, delta);
//
//  //  //sleep(1);
//  //  //printf("flipping cond sc\n");
//  //  start = read_pmccntr_el0();
//  //  sc->flip();
//  //  end = read_pmccntr_el0();
//  //  delta = end - start;
//  //  printf("wake_up_everyone: start: %llu, end: %llu, delta: %llu\n", start, end, delta);
//  //}
//
//  //printf("warmup done\n");
//
//  //l4_uint64_t se_start = 0;
//  //l4_uint64_t se_end = 0;
//  //l4_uint64_t wue_start = 0;
//  //l4_uint64_t wue_end = 0;
//  //for (int i = 0; i < 1000; i++)
//  //{
//  //  //printf("\t%d\n", i);
//  //  //sleep(1);
//  //  //printf("flipping cond sc\n");
//  //  se_start += read_pmccntr_el0();
//  //  sc->flip();
//  //  se_end += read_pmccntr_el0();
//  //  //delta = end - start;
//  //  //printf("stop_everyone: start: %llu, end: %llu, delta: %llu\n", start, end, delta);
//
//  //  //sleep(1);
//  //  //printf("flipping cond sc\n");
//  //  wue_start += read_pmccntr_el0();
//  //  sc->flip();
//  //  wue_end += read_pmccntr_el0();
//  //  //delta = end - start;
//  //  //printf("wake_up_everyone: start: %llu, end: %llu, delta: %llu\n", start, end, delta);
//  //}
//
//  //l4_uint64_t delta;
//  //delta = se_end - se_start;
//  //delta /= 1000;
//  //printf("stop_everyone: avg: %llu\n", delta);
//  //delta = wue_end - wue_start;
//  //delta /= 1000;
//  //printf("wake_up_everyone: avg: %llu\n", delta);
//
//  //sleep(2);
//  //printf("flipping cond sc\n");
//  //sc->flip();
//  //sleep(2);
//  //printf("flipping cond sc\n");
//  //sc->flip();
//
//  //pthread_join(pt_local, nullptr);
//  //pthread_join(pt_remote1, nullptr);
//  ////pthread_join(pt_remote2, nullptr);
//  ////pthread_join(pt_remote3, nullptr);
//
//  printf("benchmark done\n");
//
//  return 0;
//}

#include <l4/re/env>
#include <l4/re/util/cap_alloc>
#include <l4/sys/scheduler>
#include <l4/sys/sched_constraint>
#include <pthread-l4.h>
#include <cstdio>
#include <unistd.h>

void *worker_func(void *);

using namespace L4Re;

// Global counter to observe thread activity
volatile unsigned long counter = 0;

void *worker_func(void *) {
    while (true) {
        counter++;
    }
    return nullptr;
}

int main() {
    auto env = L4Re::Env::env();
    auto factory = env->factory();
    auto scheduler = env->scheduler();

    // Create the Scheduling Constraint (SC)
    L4::Cap<L4::Cond_sc> sc = L4Re::Util::cap_alloc.alloc<L4::Cond_sc>();
    factory->create(sc) << l4_umword_t(L4_SCHED_CONSTRAINT_TYPE_COND);
    sc->flip();

    // Create worker thread
    pthread_t worker_pt;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    attr.create_flags |= PTHREAD_L4_ATTR_NO_START;
    pthread_create(&worker_pt, &attr, worker_func, nullptr);
    L4::Cap<L4::Thread> worker(pthread_l4_cap(worker_pt));

    // Attach SC to worker
    scheduler->attach_sc(worker, sc);

    // Start worker
    l4_sched_param_t sp = l4_sched_param(100);
    sp.affinity = l4_sched_cpu_set(0, 0, 2);
    scheduler->set_cpus_sc(sc, l4_sched_cpu_set(0, 0, 2));
    scheduler->run_thread(worker, sp);

    for (int i = 0; i < 5; ++i) {
        printf("Main: Enabling worker...\n");
        sc->flip();

        usleep(500000);
        printf("Counter value: %lu\n", counter);

        printf("Main: Disabling worker...\n");
        sc->flip();

        unsigned long last_val = counter;
        usleep(500000);

        if (counter == last_val) {
            printf("Counter stayed at %lu (Worker successfully disabled)\n", counter);
        } else {
            printf("Counter moved to %lu (Something is wrong!)\n", counter);
        }
        printf("----------------------------\n");
    }

    return 0;
}
