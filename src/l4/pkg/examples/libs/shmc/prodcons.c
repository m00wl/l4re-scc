/*
 * (c) 2008-2009 Adam Lackorzynski <adam@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */

/*
 * This example uses shared memory between two threads, one producer, one
 * consumer.
 */

#include <l4/shmc/shmc.h>

#include <l4/util/util.h>

#include <stdio.h>
#include <string.h>
#include <pthread-l4.h>

#include <l4/sys/thread.h>
#include <l4/sys/debugger.h>
#include <l4/sys/kip.h>
#include <l4/re/env.h>

#define LOG(args...)            printf(NAME ": " args)
#define CHK(func)                                               \
  do                                                            \
    {                                                           \
      long r = (func);                                          \
      if (r)                                                    \
        {                                                       \
          printf(NAME ": Failure %ld (%s) at line %d.\n",       \
                 r, l4sys_errtostr(r), __LINE__);               \
          return (void *)-1;                                    \
        }                                                       \
    } while (0)

static const char some_data[] = "Hi consumer!";

static inline l4_cap_idx_t self(void) { return pthread_l4_cap(pthread_self()); }

#define NAME "PRODUCER"
static void *thread_producer(void *d)
{
  (void)d;
  l4shmc_chunk_t p_one;
  l4shmc_signal_t s_one, s_done;
  l4shmc_area_t shmarea;
  l4_kernel_clock_t try_until;

  l4_debugger_set_object_name(self(), "producer");

  // attach this thread to the shm object
  CHK(l4shmc_attach("testshm", &shmarea));

  // add a chunk
  CHK(l4shmc_add_chunk(&shmarea, "one", 1024, &p_one));

  // add a signal
  CHK(l4shmc_add_signal(&shmarea, "testshm_prod", &s_one));

  CHK(l4shmc_attach_signal(&shmarea, "testshm_done", self(), &s_done));

  // connect chunk and signal
  CHK(l4shmc_connect_chunk_signal(&p_one, &s_one));

  CHK(l4shmc_mark_client_initialized(&shmarea));

  try_until = l4_kip_clock(l4re_kip()) + 10 * 1000000;

  for (;;)
    {
      l4_umword_t clients;
      l4shmc_get_initialized_clients(&shmarea, &clients);
      if (clients == 3UL)
        break;
      if (l4_kip_clock(l4re_kip()) >= try_until)
        {
          LOG("consumer not initialized within time\n");
          return (void *)-1;
        }
    }

  LOG("Ready.\n");

  while (1)
    {
      while (l4shmc_chunk_try_to_take(&p_one))
        printf("Uh, should not happen!\n"); //l4_thread_yield();

      memcpy(l4shmc_chunk_ptr(&p_one), some_data, sizeof(some_data));

      CHK(l4shmc_chunk_ready_sig(&p_one, sizeof(some_data)));

      LOG("Sent data.\n");

      CHK(l4shmc_wait_signal(&s_done));
    }

  l4_sleep_forever();
  return NULL;
}


#undef NAME
#define NAME "CONSUMER"
static void *thread_consumer(void *d)
{
  (void)d;
  l4shmc_area_t shmarea;
  l4shmc_chunk_t p_one;
  l4shmc_signal_t s_one, s_done;
  l4_kernel_clock_t try_until;

  l4_debugger_set_object_name(self(), "consumer");

  // attach to shared memory area
  CHK(l4shmc_attach("testshm", &shmarea));

  // get chunk 'one'
  CHK(l4shmc_get_chunk(&shmarea, "one", &p_one));

  // add a signal
  CHK(l4shmc_add_signal(&shmarea, "testshm_done", &s_done));

  // attach signal to this thread
  CHK(l4shmc_attach_signal(&shmarea, "testshm_prod", self(), &s_one));

  // connect chunk and signal
  CHK(l4shmc_connect_chunk_signal(&p_one, &s_one));

  CHK(l4shmc_mark_client_initialized(&shmarea));

  try_until = l4_kip_clock(l4re_kip()) + 10 * 1000000;

  for (;;)
    {
      l4_umword_t clients;
      l4shmc_get_initialized_clients(&shmarea, &clients);
      if (clients == 3UL)
        break;
      if (l4_kip_clock(l4re_kip()) >= try_until)
        {
          LOG("producer not initialized within time\n");
          return (void *)-1;
        }
    }

  LOG("Ready.\n");

  while (1)
    {
      CHK(l4shmc_wait_chunk(&p_one));

      LOG("Received from chunk one: '%s'.\n",
          (char *)l4shmc_chunk_ptr(&p_one));
      memset(l4shmc_chunk_ptr(&p_one), 0, l4shmc_chunk_size(&p_one));

      CHK(l4shmc_chunk_consumed(&p_one));

      CHK(l4shmc_trigger(&s_done));
    }

  return NULL;
}


int main(void)
{
  pthread_t one, two;
  long r;

  // create shared memory area
  if ((r = l4shmc_create("testshm")) < 0)
    {
      printf("Error %ld (%s) creating shared memory area\n",
             r, l4sys_errtostr(r));
      return 1;
    }

  // create two threads, one for producer, one for consumer
  pthread_create(&one, 0, thread_producer, 0);
  pthread_create(&two, 0, thread_consumer, 0);

  // now sleep, the two threads are doing the work
  l4_sleep_forever();

  return 0;
}
