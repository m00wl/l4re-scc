/* -*- c -*- */

#include "globalconfig.h"
#include "linking.h"
#include "kernel.ld.inc"

_boot_sys_start = 0x300000;
_kernel_image	= FIASCO_IMAGE_VIRT_START;	/* kernel link address */
_load		= FIASCO_IMAGE_PHYS_START & 0x1fffff;
/*
_load		= 0x1000;	/* skip page 0 to preserve BIOS data and
				 * page 1 to reserve space for the KIP */

/*_fiasco_image_offset = 0xf0000000;*/
_fiasco_image_offset = FIASCO_IMAGE_PHYS_OFFSET;

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(bootstrap__boot_start)

PHDRS {
  tramp PT_LOAD;
  btext PT_LOAD;
  bdata PT_LOAD;
  kip PT_LOAD;
  koptions PT_LOAD;
  ktext PT_LOAD;
  kdata PT_LOAD;
  kitext PT_LOAD;
  kidata PT_LOAD;
  l4_kip 0x10;
  l4_koptions 0x11;
}

SECTIONS {
  . = FIASCO_MP_TRAMP_PAGE;
  .realmode_tramp : {
      *(.realmode_tramp*)
      . = ALIGN(0x1000);
  } : tramp

  . = _boot_sys_start;
  .text_boot : ALIGN(0x1000) {
    *(.bootstrap.init)
    *(.bootstrap.text .bootstrap.text.* .bootstrap.gnu.linkonce.t.*)
    *(.bootstrap.fini)

    *(.bootstrap.initcall*)
    *(.bootstrap.rodata .bootstrap.rodata.* .bootstrap.gnu.linkonce.r.*)
    *(.bootstrap.data .bootstrap.data.* .bootstrap.gnu.linkonce.d.*)
    *(.bootstrap.anno)

  } : btext

  . = ALIGN(0x10);
  .bss_boot : {
      *(.bootstrap.bss .bootstrap.gnu.linkonce.b.*)
  . = ALIGN(0x1000);
  PROVIDE(_boot_sys_end = .);
  } : bdata

  . = _kernel_image + _load;

  .kip : AT(ADDR(.kip) - _fiasco_image_offset) {
    _kernel_image_start = .;
    *(.kernel_info_page)
    _initkip_start = .;
    KEEP(*(.initkip.version))
    KEEP(*(.initkip.features))
    KEEP(*(.initkip.features.end))
    _initkip_end = .;
    /* See KIP layout in abi/kip.cpp. The feature strings are terminated by '\0'! */
    ASSERT (_initkip_end < my_kernel_info_page + 0x900, "Kernel version strings too long");
    . = ALIGN(4K);
  } :kip :l4_kip = 0

  .koptions : AT (ADDR(.koptions) - _fiasco_image_offset) {
    *(.koptions)
  } :koptions :l4_koptions = 0

#ifdef CONFIG_KERNEL_NX
  . = ALIGN(2M);
#endif

  .text : AT (ADDR(.text) - _fiasco_image_offset) {
    PROVIDE ( _kernel_text_start = . );
    *(SORT(.entry.text.*))
    PROVIDE ( _kernel_text_entry_end = . );
    . = ALIGN(8);
    crt0.o(.text)
    *(.init)
    *(.text SORT(.text.*) .gnu.linkonce.t.*)
    *(.fixup .fixup.*)
/*    *(.fini) */

#ifdef CONFIG_KERNEL_NX
    /*
     * We don't want to have another executable superpage in the kernel, so we
     * keep the initcall text here.
     */
    *(.initcall.text*)
#endif

    PROVIDE (_ecode = .);

    *(.rodata .rodata.* .gnu.linkonce.r.*)

    . = ALIGN(8);
    PROVIDE(__exc_table_start = .);
    KEEP(*(__exc_table __exc_table.*))
    PROVIDE(__exc_table_end = .);

    . = ALIGN(16);
    PROVIDE (ia32_intel_microcode_start = .);
    KEEP(*(.intel_microcode))
    PROVIDE (ia32_intel_microcode_end = .);

    . = ALIGN(8);
    JDB_TABLE(typeinfo);

    . = ALIGN(0x40);

    PROVIDE (_etext = .);
  } : ktext = 0x90909090

#ifdef CONFIG_KERNEL_NX
  /*
   * This section serves the purpose of taking up the remainder of the last 2M
   * up to a new 2M boundary without taking up any space in the file (the linker
   * script can apparently do this only for writable sections). This way we tell
   * the kernel this memory (i.e. the executable part of the kernel address
   * space) is reserved and should not be used for anything.
   */
  .fill_kernel : AT (ADDR(.fill_kernel) - _fiasco_image_offset) {
    . = ALIGN(2M);
  }
#endif

  .data : AT (ADDR(.data) - _fiasco_image_offset) {
    PROVIDE (_kernel_data_start = .);
    *(.data .data.* .gnu.linkonce.d.*)
    *(.anno)

#ifdef CONFIG_KERNEL_NX
    PROVIDE (_kernel_data_entry_start = .);
    *(SORT(.entry.data.*));
    PROVIDE (_kernel_data_entry_end = .);
#endif

    /* Constructor and destructor lists, ordered by priority.  The lists
       are traversed from end to start, therefore the *_END__ symbols
       precede the *_LIST__ symbols. */

    . = ALIGN(0x10);
    __PER_CPU_INIT_ARRAY_START__ = .;
    KEEP (*(.init_array.65534))
    KEEP (*(.init_array.65533))
    KEEP (*(.init_array.65532))
    KEEP (*(.init_array.65531))
    KEEP (*(.init_array.65530))
    KEEP (*(.init_array.65529))
    KEEP (*(.init_array.65528))
    KEEP (*(.init_array.65527))
    KEEP (*(.init_array.65526))
    KEEP (*(.init_array.65525))
    __PER_CPU_INIT_ARRAY_END__ = .;

    . = ALIGN(0x10);
    __PER_CPU_LATE_INIT_ARRAY_START__ = .;
    KEEP (*(.init_array.65524))
    KEEP (*(.init_array.65523))
    KEEP (*(.init_array.65522))
    KEEP (*(.init_array.65521))
    KEEP (*(.init_array.65520))
    KEEP (*(.init_array.65519))
    KEEP (*(.init_array.65518))
    KEEP (*(.init_array.65517))
    KEEP (*(.init_array.65516))
    KEEP (*(.init_array.65515))
    __PER_CPU_LATE_INIT_ARRAY_END__ = .;

    . = ALIGN(0x10);
    __INIT_ARRAY_START__ = .;
    KEEP (*(SORT (.init_array.*)))
    KEEP (*(.init_array))
    __INIT_ARRAY_END__ = .;

    . = ALIGN(0x10);
    __PER_CPU_CTORS_END__ = .;
    KEEP (*(.ctors.00010))
    KEEP (*(.ctors.00009))
    KEEP (*(.ctors.00008))
    KEEP (*(.ctors.00007))
    KEEP (*(.ctors.00006))
    KEEP (*(.ctors.00005))
    KEEP (*(.ctors.00004))
    KEEP (*(.ctors.00003))
    KEEP (*(.ctors.00002))
    KEEP (*(.ctors.00001))
    __PER_CPU_CTORS_LIST__ = .;

    . = ALIGN(0x10);
    __PER_CPU_LATE_CTORS_END__ = .;
    KEEP (*(.ctors.00020))
    KEEP (*(.ctors.00019))
    KEEP (*(.ctors.00018))
    KEEP (*(.ctors.00017))
    KEEP (*(.ctors.00016))
    KEEP (*(.ctors.00015))
    KEEP (*(.ctors.00014))
    KEEP (*(.ctors.00013))
    KEEP (*(.ctors.00012))
    KEEP (*(.ctors.00011))
    __PER_CPU_LATE_CTORS_LIST__ = .;

    . = ALIGN(0x10);
    __CTOR_END__ = .;
    CONSTRUCTORS
    KEEP (*(.ctors))
    KEEP (*(SORT (.ctors.*)))
    __CTOR_LIST__ = .;

    . = ALIGN(0x10);
    __DTOR_END__ = .;
    KEEP (*(SORT (.dtors.*)))
    KEEP (*(.dtors))
    __DTOR_LIST__ = .;

    . = ALIGN(8);
    JDB_TABLE(log);

    PROVIDE (_edata = .);
  } : kdata

  . = ALIGN(32);
  .per_cpu_data : AT (ADDR(.per_cpu_data) - _fiasco_image_offset) {
    PROVIDE (_per_cpu_data_start = .);
    *(.per_cpu.data)
    . = ALIGN(8);
    PROVIDE (_per_cpu_data_end = .);
  } : kdata

  . = ALIGN(0x10);
  .bss : AT (ADDR(.bss) - _fiasco_image_offset) {
    PROVIDE (_bss_start = .);
    . = ALIGN(8);
    _per_cpu_ctor_data_start = .;
    KEEP (*(.bss.per_cpu_ctor_data))
    _per_cpu_ctor_data_end = .;

    *(.bss .bss.* .gnu.linkonce.b.* COMMON)
    PROVIDE (_bss_end = .);
  } : kdata

  PROVIDE (_end = .);

  . = ALIGN(4096);
  PROVIDE (_initcall_start = .);
  .initcall.text : AT (ADDR(.initcall.text) - _fiasco_image_offset) {
#ifndef CONFIG_KERNEL_NX
    *(.initcall.text*)
#endif
  } : kitext = 0x90909090

  .initcall.data : AT (ADDR(.initcall.data) - _fiasco_image_offset) {
    *(.initcall.data*)
    . = ALIGN(4096);
    PROVIDE (_initcall_end = .);
  } : kidata

  ASSERT(_initcall_end - _kernel_image < FIASCO_IMAGE_VIRT_SIZE,
         "Increase FIASCO_IMAGE_VIRT_SIZE!")

  /DISCARD/ : {
    *(.note)
    *(.note.ABI-tag)
    *(.comment)
    *(.eh_frame)
  }
}
