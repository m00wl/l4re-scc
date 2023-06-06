/*
 * (c) 2008-2009 Adam Lackorzynski <adam@os.inf.tu-dresden.de>,
 *               Alexander Warg <warg@os.inf.tu-dresden.de>
 *     economic rights: Technische Universität Dresden (Germany)
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU Lesser General Public License 2.1.
 * Please see the COPYING-LGPL-2.1 file for details.
 */
#include <l4/shmc/shmc.h>

#include <l4/sys/err.h>
#include <l4/sys/rcv_endpoint.h>
#include <l4/re/env.h>
#include <l4/re/c/rm.h>
#include <l4/sys/debugger.h>

#include <l4/util/util.h>
#include <l4/util/atomic.h>

#include <string.h>
#include <stdio.h>

/* Head of a shared memory data memory, which has a size of multiple pages
 * No task local data must be in here (pointers, caps)
 */
typedef struct
{
  l4_umword_t  lock;                    // lock for handling chunks
  l4_umword_t  _next_client_nr;         // number of next client to attach
  l4_umword_t  _clients_init_done;      // bitmask of initialized clients
  l4_addr_t    _first_chunk;            // offset to first chunk
} shared_mem_t;

enum
{
  SHMAREA_LOCK_FREE = 0,
  SHMAREA_LOCK_TAKEN,
};

enum
{
  MAX_SIZE = (~0UL) >> 1,
};

static inline l4shmc_chunk_desc_t *
chunk_get(l4_addr_t o, void const *shm_local_addr)
{
  return (l4shmc_chunk_desc_t *)(o + (l4_addr_t)shm_local_addr);
}

L4_CV long
l4shmc_create(char const *shm_name)
{
  shared_mem_t *s;
  l4re_ds_t shm_ds;
  l4re_ds_size_t shm_size;
  long r;

  shm_ds = l4re_env_get_cap(shm_name);
  if (l4_is_invalid_cap(shm_ds))
    return -L4_ENOENT;

  shm_size = l4re_ds_size(shm_ds);
  if (shm_size > MAX_SIZE)
    return -L4_ENOMEM;

  if ((l4re_ds_flags(shm_ds) & L4_FPAGE_RW) != L4_FPAGE_RW)
    return -L4_EPERM;

  if ((r = l4re_rm_attach((void **)&s, shm_size,
                          L4RE_RM_F_SEARCH_ADDR | L4RE_RM_F_RW,
                          shm_ds | L4_CAP_FPAGE_RW,
                          0, L4_PAGESHIFT)))
    return r;

  // Do NOT initialize any members! We assume that the passed dataspace has
  // been zero-initialized. Certain setups assume that both sides may create
  // the shared memory area.
  //
  // Here we assume
  // - _first_chunk = 0UL, and
  // - lock = SHMAREA_LOCK_FREE.

  l4re_rm_detach_unmap((l4_addr_t)s, L4RE_THIS_TASK_CAP);
  return L4_EOK;
}


L4_CV long
l4shmc_attach(char const *shm_name, l4shmc_area_t *shmarea)
{
  l4re_ds_t shm_ds;
  l4re_ds_size_t shm_size;
  shared_mem_t *shm_addr;
  long ret;

  strncpy(shmarea->_name, shm_name, sizeof(shmarea->_name));
  shmarea->_name[sizeof(shmarea->_name) - 1] = 0;
  shmarea->_local_addr = 0;

  shm_ds = l4re_env_get_cap(shm_name);
  if (l4_is_invalid_cap(shm_ds))
    return -L4_ENOENT;

  if ((l4re_ds_flags(shm_ds) & L4_FPAGE_RW) != L4_FPAGE_RW)
    return -L4_EPERM;

  shm_size = l4re_ds_size(shm_ds);
  if (shm_size > MAX_SIZE)
    return -L4_ENOMEM;

  shmarea->_shm_ds = shm_ds;
  shmarea->_size = shm_size;

  if ((ret = l4re_rm_attach(&shmarea->_local_addr, shmarea->_size,
                            L4RE_RM_F_SEARCH_ADDR | L4RE_RM_F_RW,
                            shmarea->_shm_ds | L4_CAP_FPAGE_RW,
                            0, L4_PAGESHIFT) < 0))
    return ret;

  shm_addr = (shared_mem_t *)shmarea->_local_addr;
  shmarea->_client_nr = __atomic_fetch_add(&shm_addr->_next_client_nr, 1UL,
                                           __ATOMIC_SEQ_CST);

  return L4_EOK;
}

L4_CV long
l4shmc_mark_client_initialized(l4shmc_area_t *shmarea)
{
  shared_mem_t *shm_addr = (shared_mem_t *)shmarea->_local_addr;
  l4_umword_t n = shmarea->_client_nr;

  if (n >= sizeof(l4_umword_t) * 8)
    return -L4_EINVAL;

  __atomic_or_fetch(&shm_addr->_clients_init_done, 1UL << n, __ATOMIC_SEQ_CST);
  return L4_EOK;
}

L4_CV long
l4shmc_get_client_nr(l4shmc_area_t const *shmarea)
{
  return shmarea->_client_nr;
}

L4_CV long
l4shmc_get_initialized_clients(l4shmc_area_t *shmarea, l4_umword_t *bitmask)
{
  shared_mem_t *shm_addr = (shared_mem_t *)shmarea->_local_addr;

  *bitmask = __atomic_load_n(&shm_addr->_clients_init_done, __ATOMIC_SEQ_CST);
  return L4_EOK;
}

L4_CV long
l4shmc_area_overhead(void)
{
  return sizeof(shared_mem_t);
}

L4_CV long
l4shmc_area_size(l4shmc_area_t const *shmarea)
{
  return l4re_ds_size(shmarea->_shm_ds);
}

L4_CV long
l4shmc_chunk_overhead(void)
{
  // The function l4shmc_add_chunk() aligns the passed chunk size to l4_addr_t.
  // Consider this here!
  return sizeof(l4shmc_chunk_desc_t) + sizeof(l4_addr_t);
}

static long next_chunk(l4shmc_area_t const *shmarea, l4_addr_t offs)
{
  shared_mem_t *shm_addr = (shared_mem_t *)shmarea->_local_addr;
  volatile l4shmc_chunk_desc_t *p;
  l4_addr_t next;

  if (offs == 0)
    next = shm_addr->_first_chunk;
  else
    {
      p = chunk_get(offs, shmarea->_local_addr);
      next = p->_next;
    }
  if (next == 0)
    return 0;
  if (next >= shmarea->_size || next + sizeof(*p) >= shmarea->_size || next <= offs)
    return -L4_EIO;
  if (next % sizeof(l4_addr_t) != 0)
    return -L4_EINVAL;
  p = chunk_get(next, shmarea->_local_addr);
  if (p->_magic != L4SHMC_CHUNK_MAGIC)
    return -L4_EIO;
  return next;
}

L4_CV long
l4shmc_iterate_chunk(l4shmc_area_t const *shmarea, char const **chunk_name,
                     long offs)
{
  if (offs < 0)
    return -L4_EINVAL;
  offs = next_chunk(shmarea, offs);
  if (offs > 0)
    {
      l4shmc_chunk_desc_t *p = chunk_get(offs, shmarea->_local_addr);
      *chunk_name = p->_name;
    }
  return offs;
}

L4_CV long
l4shmc_add_chunk(l4shmc_area_t *shmarea, char const *chunk_name,
                l4_umword_t chunk_capacity, l4shmc_chunk_t *chunk)
{
  shared_mem_t *shm_addr = (shared_mem_t *)shmarea->_local_addr;

  l4shmc_chunk_desc_t *p = NULL;
  l4shmc_chunk_desc_t *prev = NULL;
  l4_addr_t offs = 0;
  long ret;

  chunk_capacity = (chunk_capacity + sizeof(l4_addr_t) - 1) & ~(sizeof(l4_addr_t) - 1);
  if (chunk_capacity >> (sizeof(chunk_capacity) * 8 - 1))
    return -L4_ENOMEM;

  while (!l4util_cmpxchg(&shm_addr->lock, SHMAREA_LOCK_FREE,
                         SHMAREA_LOCK_TAKEN))
    l4_sleep(1);
  asm volatile ("" : : : "memory");
  while ((ret = next_chunk(shmarea, offs)) > 0)
    {
      p = chunk_get(ret, shmarea->_local_addr);
      if (strcmp(p->_name, chunk_name) == 0)
        {
          ret = -L4_EEXIST;
          goto out_free_lock;
        }
      offs = ret;
    }
  if (ret < 0)
     goto out_free_lock;
  if (offs == 0)
    offs = sizeof(shared_mem_t);
  else
    {
      l4_addr_t n = p->_offset + p->_capacity + sizeof(*p);
      if (n <= offs || n >= shmarea->_size)
        {
          ret = -L4_EIO;
          goto out_free_lock;
        }
      offs = n;
      prev = p;
    }

  if (offs + chunk_capacity + sizeof(*p) > (unsigned long)shmarea->_size)
    {
      ret = -L4_ENOMEM;
      goto out_free_lock; // no more free memory in this shm
    }
  p = chunk_get(offs, shmarea->_local_addr);
  p->_offset = offs;
  p->_next = 0;
  p->_capacity = chunk_capacity;
  p->_size   = 0;
  p->_status = L4SHMC_CHUNK_CLEAR;
  p->_magic  = L4SHMC_CHUNK_MAGIC;
  strncpy(p->_name, chunk_name, sizeof(p->_name));
  p->_name[sizeof(p->_name) - 1] = 0;
  // Ensure that other CPUs have correct data before inserting chunk
  __sync_synchronize();

  if (prev)
    prev->_next = offs;
  else
    shm_addr->_first_chunk = offs;

  __sync_synchronize();
  shm_addr->lock = SHMAREA_LOCK_FREE;

  chunk->_chunk    = p;
  chunk->_shm      = shmarea;
  chunk->_sig      = NULL;
  chunk->_capacity = chunk_capacity;

  return L4_EOK;
out_free_lock:
  shm_addr->lock = SHMAREA_LOCK_FREE;
  return ret;
}

L4_CV long
l4shmc_area_size_free(l4shmc_area_t const *shmarea)
{
  long ret;
  l4_addr_t offs = 0;
  while ((ret = next_chunk(shmarea, offs)) > 0)
    offs = ret;
  if (ret < 0)
    return ret;
  ret = shmarea->_size - offs;
  return ret > 0 ? ret : 0;
}

L4_CV long
l4shmc_add_signal(l4shmc_area_t *shmarea, char const *signal_name,
                 l4shmc_signal_t *signal)
{
  return l4shmc_get_signal(shmarea, signal_name, signal);
}

L4_CV long
l4shmc_get_chunk_to(l4shmc_area_t *shmarea, char const *chunk_name,
                    l4_umword_t timeout_ms, l4shmc_chunk_t *chunk)
{
  l4_kernel_clock_t try_until = l4_kip_clock(l4re_kip()) + (timeout_ms * 1000);
  long ret;

  do
    {
      l4_addr_t offs = 0;
      while ((ret = next_chunk(shmarea, offs)) > 0)
        {
          l4shmc_chunk_desc_t *p;
          offs = ret;
          p = chunk_get(offs, shmarea->_local_addr);
          if (!strcmp(p->_name, chunk_name))
            { // found it!
               chunk->_shm      = shmarea;
               chunk->_chunk    = p;
               chunk->_sig      = NULL;
               chunk->_capacity = p->_capacity;
               if (   chunk->_capacity > shmarea->_size
                   || chunk->_capacity + offs > shmarea->_size)
                  return -L4_EIO;
               return L4_EOK;
            }
        }
      if (ret < 0)
        return ret;

      if (!timeout_ms)
        break;

      l4_sleep(100);
    }
  while (l4_kip_clock(l4re_kip()) < try_until);

  return -L4_ENOENT;
}

L4_CV long
l4shmc_attach_signal(l4shmc_area_t *shmarea, char const *signal_name,
                     l4_cap_idx_t thread, l4shmc_signal_t *signal)
{
  long r;

  if ((r = l4shmc_get_signal(shmarea, signal_name, signal)) < 0)
    return r;

  if ((r = l4_error(l4_rcv_ep_bind_thread(signal->_sigcap, thread,
                                          ((l4_umword_t)signal)))) < 0)
    return r;

  return L4_EOK;
}

L4_CV long
l4shmc_get_signal(l4shmc_area_t *shmarea, char const *signal_name,
                  l4shmc_signal_t *signal)
{
  (void)shmarea; // maybe link signal to shmarea?

  l4_cap_idx_t irq = l4re_env_get_cap(signal_name);
  if (l4_is_invalid_cap(irq))
    return -L4_ENOENT;

  signal->_sigcap = irq;
  return L4_EOK;
}

L4_CV long
l4shmc_connect_chunk_signal(l4shmc_chunk_t *chunk, l4shmc_signal_t *signal)
{
  chunk->_sig = signal;
  return L4_EOK;
}

L4_CV long
l4shmc_enable_signal(l4shmc_signal_t *s)
{
  return l4_error(l4_irq_unmask(s->_sigcap));
}

L4_CV long
l4shmc_enable_chunk(l4shmc_chunk_t *p)
{
  return l4shmc_enable_signal(p->_sig);
}

L4_CV long
l4shmc_wait_any_to(l4_timeout_t timeout, l4shmc_signal_t **p)
{
  l4_umword_t l;
  long r;

  if ((r = l4_ipc_error(l4_ipc_wait(l4_utcb(), &l, timeout), l4_utcb())))
    return r;

  *p = (l4shmc_signal_t *)l;

  return L4_EOK;
}

L4_CV long
l4shmc_wait_signal_to(l4shmc_signal_t *s, l4_timeout_t timeout)
{
  long r;

  if ((r = l4_ipc_error(l4_irq_receive(s->_sigcap, timeout), l4_utcb())))
    return r;

  return L4_EOK;
}

L4_CV long
l4shmc_wait_chunk_to(l4shmc_chunk_t *p, l4_timeout_t to)
{
  return l4shmc_wait_signal_to(p->_sig, to);
}
