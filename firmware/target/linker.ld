/* Memory layout of the emulated Cortex-M3 boards (256 KiB flash, 64 KiB SRAM). */

ENTRY(Reset_Handler)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 256K
    SRAM  (rwx) : ORIGIN = 0x20000000, LENGTH = 64K
}

_estack = ORIGIN(SRAM) + LENGTH(SRAM);

SECTIONS
{
    .vectors : {
        KEEP(*(.vectors))
    } > FLASH

    .text : {
        *(.text*)
        *(.rodata*)
        . = ALIGN(4);
        _etext = .;
    } > FLASH

    .data : {
        . = ALIGN(4);
        _sdata = .;
        *(.data*)
        . = ALIGN(4);
        _edata = .;
    } > SRAM AT > FLASH
    _sidata = LOADADDR(.data);

    .bss : {
        . = ALIGN(4);
        _sbss = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _ebss = .;
    } > SRAM

    /*
     * Survives a warm reset triggered by the watchdog (SYSRESETREQ). The
     * startup code deliberately does not clear this section, so the firmware
     * can report how many times the watchdog had to rescue it.
     */
    .noinit (NOLOAD) : {
        . = ALIGN(4);
        *(.noinit*)
        . = ALIGN(4);
    } > SRAM

    /DISCARD/ : {
        *(.ARM.exidx*)
        *(.comment)
        *(.note*)
    }
}
