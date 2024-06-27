#include <omp.h>
#include <stdio.h>
#include <unistd.h>

#include <l4/re/env.h>
#include <l4/sys/kip.h>

#define N_THREADS 512

char done[N_THREADS];

int main(void)
{
  int id;
  //l4_kernel_clock_t start, end;
  l4_uint64_t start, end;
  printf("Program launching\n");
  omp_set_num_threads(N_THREADS);
  //start = l4_kip_clock(l4re_kip());
  asm volatile ("mrs %0, PMCCNTR_EL0" : "=r" (start));
#pragma omp parallel private (id)
  {
    id = omp_get_thread_num();
    done[id] = 1;
    //sleep(10);
  }
  //end = l4_kip_clock(l4re_kip());
  asm volatile ("mrs %0, PMCCNTR_EL0" : "=r" (end));
  //double used = ((double) (end - start)) / 1000000;
  //printf("time: %.8f s\n", used);
  printf("cycle delta: 0x%llX\n", end - start);
  printf("DONE\n");
  return 1;
}
