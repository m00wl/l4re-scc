#pragma once

#include <l4/sys/kernel_object.h>
#include <l4/sys/ipc.h>

enum L4_sched_constraint_type
{
    L4_SCHED_CONSTRAINT_TYPE_COND,
    L4_SCHED_CONSTRAINT_TYPE_QUANT,
    L4_SCHED_CONSTRAINT_TYPE_BUDGET,
    L4_SCHED_CONSTRAINT_TYPE_TIMER_WINDOW,
};
