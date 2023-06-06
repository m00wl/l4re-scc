/* SPDX-License-Identifier: GPL-2.0-only or License-Ref-kk-custom */
/*
 * Copyright (C) 2022 Kernkonzept GmbH.
 * Author(s): Steffen Liebergeld <steffen.liebergeld@kernkonzept.com>
 *
 */

/*
 * Simplistic driver for the pl031 RTC. Does not support write.
 */

#include <l4/re/env>
#include <l4/re/rm>
#include <l4/re/error_helper>
#include <l4/util/util.h>
#include <l4/io/io.h>
#include <l4/vbus/vbus>
#include <l4/drivers/hw_mmio_register_block>

#include <cstdio>
#include <time.h>

#include "rtc.h"

static bool debug = false;

struct Pl031_rtc : Rtc
{
  bool probe()
  {
    auto vbus = L4Re::chkcap(L4Re::Env::env()->get_cap<L4vbus::Vbus>("vbus"),
                             "Get 'vbus' capability.", -L4_ENOENT);

    L4vbus::Device dev;
    l4vbus_device_t devinfo;

    if (vbus->root().device_by_hid(&dev, "arm,pl031", L4VBUS_MAX_DEPTH, &devinfo) < 0)
      return false;

    for (unsigned i = 0; i < devinfo.num_resources; ++i)
      {
        l4vbus_resource_t res;

        L4Re::chksys(dev.get_resource(i, &res),
                     "Get shared memory resources");

        if (res.type != L4VBUS_RESOURCE_MEM)
          continue;

        auto iods =
          L4::Ipc::make_cap_rw(L4::cap_reinterpret_cast<L4Re::Dataspace>(vbus));

        l4_size_t sz = res.end - res.start + 1;
        l4_addr_t addr = 0;
        if (debug)
          printf("Found resource %p..%p, sz=0x%zx\n",
                 (void*)res.start, (void*)res.end, sz);
        const L4Re::Env *env = L4Re::Env::env();
        L4Re::chksys(env->rm()->attach(&addr, sz,
                                       L4Re::Rm::F::Search_addr
                                       | L4Re::Rm::F::Cache_uncached
                                       | L4Re::Rm::F::RW,
                                       iods, res.start, L4_PAGESHIFT),
                     "Attach rtc io memory");
        _regs = new L4drivers::Mmio_register_block<32>(addr);

        time_t t = _regs[0];
        printf("Found a pl031 device. Current time: %s", ctime(&t));
        break;
      }
    return true;
  };

  int set_time(l4_uint64_t /* offset */)
  {
    return 0;
  }

  int get_time(l4_uint64_t *nsec_offset_1970)
  {
    l4_uint64_t ns = ((l4_uint64_t)_regs[0]) * 1000000000;
    *nsec_offset_1970 = ns - l4_kip_clock_ns(l4re_kip());
    return 0;
  }

private:
  L4drivers::Register_block<32> _regs;
};

static Pl031_rtc _pl031_rtc;
