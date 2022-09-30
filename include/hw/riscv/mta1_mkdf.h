/*
 * QEMU RISC-V Board Compatible with Mullvad MTA1-MKDF platform
 *
 * Copyright (c) 2022 Mullvad VPN AB
 *
 * Provides a board compatible with the Mullvad MTA1-MKDF platform:
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HW_MTA1_MKDF_H
#define HW_MTA1_MKDF_H

#include "hw/boards.h"
#include "hw/riscv/riscv_hart.h"
#include "qom/object.h"
#include "chardev/char-fe.h"

#include "hw/riscv/mta1_mkdf_mem.h"

// 18 MHz
#define MTA1_MKDF_CLOCK_FREQ 18000000
#define MTA1_MKDF_RX_FIFO_SIZE 16

typedef struct MTA1MKDFState {
    /*< private >*/
    MachineState parent_obj;

    /*< public >*/
    RISCVHartArrayState cpus;
    MemoryRegion rom;
    MemoryRegion mmio;

    QEMUTimer *qtimer;

    CharBackend fifo_chr;
    char *fifo_chr_name;
    uint8_t fifo_rx[MTA1_MKDF_RX_FIFO_SIZE];
    uint8_t fifo_rx_len;
    bool app_mode;
    uint32_t app_addr;
    uint32_t app_size;
    uint32_t uds[8]; // 32 bytes
    bool block_uds[8];
    uint32_t uda[4]; // 16 bytes
    uint32_t led;
    uint32_t cdi[8]; // 32 bytes
    uint32_t udi[2]; // 8 bytes
    uint8_t fw_ram[MTA1_MKDF_MMIO_FW_RAM_SIZE];
    uint32_t timer;
    uint32_t timer_prescaler;
    bool timer_running;
    uint32_t timer_interval;
} MTA1MKDFState;

#define TYPE_MTA1_MKDF_MACHINE MACHINE_TYPE_NAME("mta1_mkdf")
#define MTA1_MKDF_MACHINE(obj) \
    OBJECT_CHECK(MTA1MKDFState, (obj), TYPE_MTA1_MKDF_MACHINE)

enum {
    MTA1_MKDF_ROM,
    MTA1_MKDF_RAM,
    MTA1_MKDF_MMIO,
};

#endif
