#pragma once
#if __riscv_xlen == 32
#include "libgomp_f_riscv32.h"
#else
#include "libgomp_f_riscv64.h"
#endif
