/*
 * QEMU RISC-V Board Compatible with Mullvad MTA1-MKDF platform
 *
 * Copyright (c) 2022 Tillitis AB
 * SPDX-License-Identifier: GPL-2.0-only
 */

// clang-format off

#ifndef HW_MTA1_MKDF_MEM_H
#define HW_MTA1_MKDF_MEM_H

// The canonical location of this file is:
// repo: https://github.com/tillitis/tillitis-key1
// path: /hw/application_fpga/fw/mta1_mkdf_mem.h

// The contents are derived from the Verilog code. For use by QEMU model,
// firmware, and apps.

enum {
    MTA1_MKDF_ROM_BASE       = 0x00000000, // 0b00000000...
    MTA1_MKDF_RAM_BASE       = 0x40000000, // 0b01000000...
    MTA1_MKDF_RESERVED_BASE  = 0x80000000, // 0b10000000...
    MTA1_MKDF_MMIO_BASE      = 0xc0000000, // 0b11000000...
    MTA1_MKDF_MMIO_SIZE      = 0xffffffff - MTA1_MKDF_MMIO_BASE,

    MTA1_MKDF_MMIO_TRNG_BASE        = MTA1_MKDF_MMIO_BASE | 0x00000000,
    MTA1_MKDF_MMIO_TIMER_BASE       = MTA1_MKDF_MMIO_BASE | 0x01000000,
    MTA1_MKDF_MMIO_UDS_BASE         = MTA1_MKDF_MMIO_BASE | 0x02000000,
    MTA1_MKDF_MMIO_UART_BASE        = MTA1_MKDF_MMIO_BASE | 0x03000000,
    MTA1_MKDF_MMIO_TOUCH_BASE       = MTA1_MKDF_MMIO_BASE | 0x04000000,
    // This "core" only exists in QEMU
    MTA1_MKDF_MMIO_QEMU_BASE        = MTA1_MKDF_MMIO_BASE | 0x3e000000,
    MTA1_MKDF_MMIO_MTA1_BASE        = MTA1_MKDF_MMIO_BASE | 0x3f000000, // 0xff000000

    MTA1_MKDF_NAME0_SUFFIX   = 0x00,
    MTA1_MKDF_NAME1_SUFFIX   = 0x04,
    MTA1_MKDF_VERSION_SUFFIX = 0x08,

    MTA1_MKDF_MMIO_TRNG_STATUS      = MTA1_MKDF_MMIO_TRNG_BASE | 0x24,
    MTA1_MKDF_MMIO_TRNG_STATUS_READY_BIT = 0,
    MTA1_MKDF_MMIO_TRNG_ENTROPY     = MTA1_MKDF_MMIO_TRNG_BASE | 0x80,

    MTA1_MKDF_MMIO_TIMER_CTRL       = MTA1_MKDF_MMIO_TIMER_BASE | 0x20,
    MTA1_MKDF_MMIO_TIMER_CTRL_START_BIT = 0,
    MTA1_MKDF_MMIO_TIMER_CTRL_STOP_BIT  = 1,
    MTA1_MKDF_MMIO_TIMER_STATUS     = MTA1_MKDF_MMIO_TIMER_BASE | 0x24,
    MTA1_MKDF_MMIO_TIMER_STATUS_READY_BIT = 0,
    MTA1_MKDF_MMIO_TIMER_PRESCALER  = MTA1_MKDF_MMIO_TIMER_BASE | 0x28,
    MTA1_MKDF_MMIO_TIMER_TIMER      = MTA1_MKDF_MMIO_TIMER_BASE | 0x2c,

    MTA1_MKDF_MMIO_UDS_FIRST        = MTA1_MKDF_MMIO_UDS_BASE | 0x40,
    MTA1_MKDF_MMIO_UDS_LAST         = MTA1_MKDF_MMIO_UDS_BASE | 0x5c, // Address of last 32-bit word of UDS

    MTA1_MKDF_MMIO_UART_BIT_RATE    = MTA1_MKDF_MMIO_UART_BASE | 0x40,
    MTA1_MKDF_MMIO_UART_DATA_BITS   = MTA1_MKDF_MMIO_UART_BASE | 0x44,
    MTA1_MKDF_MMIO_UART_STOP_BITS   = MTA1_MKDF_MMIO_UART_BASE | 0x48,
    MTA1_MKDF_MMIO_UART_RX_STATUS   = MTA1_MKDF_MMIO_UART_BASE | 0x80,
    MTA1_MKDF_MMIO_UART_RX_DATA     = MTA1_MKDF_MMIO_UART_BASE | 0x84,
    MTA1_MKDF_MMIO_UART_TX_STATUS   = MTA1_MKDF_MMIO_UART_BASE | 0x100,
    MTA1_MKDF_MMIO_UART_TX_DATA     = MTA1_MKDF_MMIO_UART_BASE | 0x104,

    MTA1_MKDF_MMIO_TOUCH_STATUS     = MTA1_MKDF_MMIO_TOUCH_BASE | 0x24,
    MTA1_MKDF_MMIO_TOUCH_STATUS_EVENT_BIT = 0,

    // TODO HW core/addr is not yet defined for this:
    MTA1_MKDF_MMIO_QEMU_UDA         = MTA1_MKDF_MMIO_QEMU_BASE | 0x20,
    // This will only ever exist in QEMU:
    MTA1_MKDF_MMIO_QEMU_DEBUG       = MTA1_MKDF_MMIO_QEMU_BASE | 0x1000,

    MTA1_MKDF_MMIO_MTA1_NAME0       = MTA1_MKDF_MMIO_MTA1_BASE | MTA1_MKDF_NAME0_SUFFIX,
    MTA1_MKDF_MMIO_MTA1_NAME1       = MTA1_MKDF_MMIO_MTA1_BASE | MTA1_MKDF_NAME1_SUFFIX,
    MTA1_MKDF_MMIO_MTA1_VERSION     = MTA1_MKDF_MMIO_MTA1_BASE | MTA1_MKDF_VERSION_SUFFIX,
    MTA1_MKDF_MMIO_MTA1_SWITCH_APP  = MTA1_MKDF_MMIO_MTA1_BASE | 0x20,
    MTA1_MKDF_MMIO_MTA1_LED         = MTA1_MKDF_MMIO_MTA1_BASE | 0x24,
    MTA1_MKDF_MMIO_MTA1_LED_R_BIT   = 2,
    MTA1_MKDF_MMIO_MTA1_LED_G_BIT   = 1,
    MTA1_MKDF_MMIO_MTA1_LED_B_BIT   = 0,
    MTA1_MKDF_MMIO_MTA1_GPIO        = MTA1_MKDF_MMIO_MTA1_BASE | 0x28,
    MTA1_MKDF_MMIO_MTA1_GPIO1_BIT   = 0,
    MTA1_MKDF_MMIO_MTA1_GPIO2_BIT   = 1,
    MTA1_MKDF_MMIO_MTA1_GPIO3_BIT   = 2,
    MTA1_MKDF_MMIO_MTA1_GPIO4_BIT   = 3,
    MTA1_MKDF_MMIO_MTA1_APP_ADDR    = MTA1_MKDF_MMIO_MTA1_BASE | 0x30, // 0x4000_0000
    MTA1_MKDF_MMIO_MTA1_APP_SIZE    = MTA1_MKDF_MMIO_MTA1_BASE | 0x34,
    MTA1_MKDF_MMIO_MTA1_DEBUG       = MTA1_MKDF_MMIO_MTA1_BASE | 0x40,
    MTA1_MKDF_MMIO_MTA1_CDI_FIRST   = MTA1_MKDF_MMIO_MTA1_BASE | 0x80,
    MTA1_MKDF_MMIO_MTA1_CDI_LAST    = MTA1_MKDF_MMIO_MTA1_BASE | 0x9c, // Address of last 32-bit word of CDI.
    MTA1_MKDF_MMIO_MTA1_UDI_FIRST   = MTA1_MKDF_MMIO_MTA1_BASE | 0xc0,
    MTA1_MKDF_MMIO_MTA1_UDI_LAST    = MTA1_MKDF_MMIO_MTA1_BASE | 0xc4, // Address of last 32-bit word of UDI.
};

#endif
