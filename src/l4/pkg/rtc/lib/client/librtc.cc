/**
 * \file   rtc/lib/client/librtc.cc
 * \brief  client stub
 *
 * (c) 2014 Steffen Liebergeld <steffen.liebergeld@kernkonzept.com>
 *
 * Based on work by Frank Mehnert:
 * \date   09/23/2003
 * \author Frank Mehnert <fm3@os.inf.tu-dresden.de> */

/*
 * (c) 2003-2009 Author(s)
 *     economic rights: Technische Universität Dresden (Germany)
 *
 * This file is part of TUD:OS and distributed under the terms of the
 * GNU General Public License 2.
 * Please see the COPYING-GPL-2 file for details.
 */

#include <l4/sys/err.h>
#include <l4/rtc/rtc.h>
#include <l4/sys/types.h>
#include <l4/rtc/rtc>
#include <l4/re/env>
#include <l4/sys/kip.h>

static inline
l4_uint64_t get_timer()
{
  return l4_kip_clock_ns(l4re_kip());
}

L4rtc::Rtc::Time L4rtc::Rtc::get_timer()
{ return ::get_timer(); }

l4_uint64_t l4rtc_get_timer()
{ return get_timer(); }

/**
 * Deliver the offset between real time and system's uptime in seconds and
 * nanoseconds.
 * Some applications want to compute their time in other ways as done
 * in l4rtc_get_seconds_since_1970(). */
int
l4rtc_get_offset_to_realtime(l4_cap_idx_t server, l4_uint64_t *nanoseconds)
{
  return L4::Cap<L4rtc::Rtc>(server)->get_timer_offset(nanoseconds);
}

int
l4rtc_set_offset_to_realtime(l4_cap_idx_t server, l4_uint64_t nanoseconds)
{
  return L4::Cap<L4rtc::Rtc>(server)->set_timer_offset(nanoseconds);
}

