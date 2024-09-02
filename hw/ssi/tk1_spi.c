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

#include "qemu/osdep.h"
#include "hw/irq.h"
#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "hw/ssi/tk1_spi.h"

#define EN      0x00
#define XFER    0x04
#define DATA    0x08

static void tk1_spi_reset(DeviceState *d)
{
    TK1SPIState *s = TK1_SPI(d);

    s->data = 0;
}

static uint64_t tk1_spi_read(void *opaque, hwaddr addr, unsigned int size)
{
    TK1SPIState *s = opaque;

    switch (addr) {
        case EN:
            return 0;
        case XFER:
            // Since emulated transfers are instantaneous. We always say we're
            // ready.
            return 1;
        case DATA:
            return s->data;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read: addr=0x%"
                  HWADDR_PRIx "\n", __func__, addr);
    return 0;
}

static void tk1_spi_write(void *opaque, hwaddr addr,
                             uint64_t val64, unsigned int size)
{
    TK1SPIState *s = opaque;
    uint32_t value = val64;

    switch (addr) {
        case EN:
            qemu_set_irq(s->cs_line, !(value & 1));
            return;
        case XFER:
            s->data = ssi_transfer(s->spi, s->data);
            return;
        case DATA:
            s->data = value;
            return;
    }

    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write: addr=0x%"
                  HWADDR_PRIx " value=0x%x\n", __func__, addr, value);
    return;
}

static const MemoryRegionOps tk1_spi_ops = {
    .read = tk1_spi_read,
    .write = tk1_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4
    }
};

static void tk1_spi_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    TK1SPIState *s = TK1_SPI(dev);

    s->spi = ssi_create_bus(dev, "spi");
    sysbus_init_irq(sbd, &s->irq);
    sysbus_init_irq(sbd, &s->cs_line);

    memory_region_init_io(&s->mmio, OBJECT(s), &tk1_spi_ops, s,
                          TYPE_TK1_SPI, TK1_SPI_SIZE);
    sysbus_init_mmio(sbd, &s->mmio);
}

static void tk1_spi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->reset = tk1_spi_reset;
    dc->realize = tk1_spi_realize;
}

static const TypeInfo tk1_spi_info = {
    .name           = TYPE_TK1_SPI,
    .parent         = TYPE_SYS_BUS_DEVICE,
    .instance_size  = sizeof(TK1SPIState),
    .class_init     = tk1_spi_class_init,
};

static void tk1_spi_register_types(void)
{
    type_register_static(&tk1_spi_info);
}

type_init(tk1_spi_register_types)
