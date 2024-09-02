/*
 * QEMU model of the Tillitis TK1 SPI Controller
 *
 * Copyright (c) 2024 Tillitis AB
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

#ifndef HW_TK1_SPI_H
#define HW_TK1_SPI_H

#include "hw/sysbus.h"

#define TYPE_TK1_SPI "tk1.spi"
#define TK1_SPI(obj) OBJECT_CHECK(TK1SPIState, (obj), TYPE_TK1_SPI)

#define TK1_SPI_SIZE 0x12

typedef struct TK1SPIState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    qemu_irq irq;

    qemu_irq cs_line;

    SSIBus *spi;

    uint32_t data;
} TK1SPIState;

#endif /* HW_TK1_SPI_H */
