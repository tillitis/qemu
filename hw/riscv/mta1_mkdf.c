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

#include "qemu/osdep.h"
#include "qemu/cutils.h"
#include "hw/riscv/mta1_mkdf.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/misc/unimp.h"
#include "hw/riscv/boot.h"
#include "qemu/units.h"
#include "sysemu/sysemu.h"
#include "hw/riscv/mullvad_cpu.h"
#include "hw/char/riscv_htif.h"
#include "qapi/qmp/qerror.h"
#include "qemu/log.h"

static const MemMapEntry mta1_mkdf_memmap[] = {
    [MTA1_MKDF_ROM] =  {     0x1000, 0x20000 /*128K*/ },
    [MTA1_MKDF_RAM] =  { 0x80000000, 0x20000 },
    [MTA1_MKDF_MMIO] = { 0x90000000, 0x20000 },
};

enum {
    MTA1_MKDF_MMIO_UDS           = 0x0,
    MTA1_MKDF_MMIO_UDA           = 0x20,
    MTA1_MKDF_MMIO_SWITCH_APP    = 0x30,
    MTA1_MKDF_MMIO_UDI           = 0x200,
    MTA1_MKDF_MMIO_NAME0         = 0x208,
    MTA1_MKDF_MMIO_NAME1         = 0x20c,
    MTA1_MKDF_MMIO_VERSION       = 0x210,
    MTA1_MKDF_MMIO_RX_FIFO_AVAIL = 0x214,
    MTA1_MKDF_MMIO_RX_FIFO       = 0x215,
    MTA1_MKDF_MMIO_TX_FIFO_AVAIL = 0x216,
    MTA1_MKDF_MMIO_TX_FIFO       = 0x217,
    MTA1_MKDF_MMIO_LED           = 0x218,
    MTA1_MKDF_MMIO_COUNTER       = 0x21c,
    MTA1_MKDF_MMIO_TRNG_STATUS   = 0x220,
    MTA1_MKDF_MMIO_TRNG_DATA     = 0x224,
    MTA1_MKDF_MMIO_CDI           = 0x400,
    MTA1_MKDF_MMIO_APP_ADDR      = 0x420, /* 0x8000_0000 */
    MTA1_MKDF_MMIO_APP_SIZE      = 0x424,
};

static bool mta1_mkdf_setup_chardev(MTA1MKDFState *s, Error **errp)
{
    Chardev *chr;

    if (!s->fifo_chr_name) {
        error_setg(errp, QERR_INVALID_PARAMETER_VALUE,
                   "fifo", "a valid character device");
        return false;
    }

    chr = qemu_chr_find(s->fifo_chr_name);
    if (!chr) {
        error_set(errp, ERROR_CLASS_DEVICE_NOT_FOUND,
                  "Device '%s' not found", s->fifo_chr_name);
        return false;
    }

    return qemu_chr_fe_init(&s->fifo_chr, chr, errp);
}

static void mta1_mkdf_fifo_rx(void *opaque, const uint8_t *buf, int size)
{
    MTA1MKDFState *s = opaque;

    if (s->fifo_rx_len >= sizeof(s->fifo_rx)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: FIFO Rx dropped! size=%d\n", __func__, size);
        return;
    }
    s->fifo_rx[s->fifo_rx_len++] = *buf;
}

static int mta1_mkdf_fifo_can_rx(void *opaque)
{
    MTA1MKDFState *s = opaque;

    return s->fifo_rx_len < sizeof(s->fifo_rx);
}

static void mta1_mkdf_fifo_event(void *opaque, QEMUChrEvent event)
{
}

static int mta1_mkdf_fifo_be_change(void *opaque)
{
    MTA1MKDFState *s = opaque;

    qemu_chr_fe_set_handlers(&s->fifo_chr, mta1_mkdf_fifo_can_rx, mta1_mkdf_fifo_rx,
                             mta1_mkdf_fifo_event, mta1_mkdf_fifo_be_change, s,
                             NULL, true);

    return 0;
}


static void mta1_mkdf_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    MTA1MKDFState *s = opaque;
    uint8_t c = val;
    hwaddr end = addr + size;

    /* CDI u8[32] */
    if (addr >= MTA1_MKDF_MMIO_CDI && end <= MTA1_MKDF_MMIO_CDI + 32) {
        if (s->app_mode) {
            goto bad;
        } else {
            memcpy(&s->cdi[addr - MTA1_MKDF_MMIO_CDI], &val, size);
        }
    }

    switch (addr) {
    case MTA1_MKDF_MMIO_SWITCH_APP:
        if (val != 0) {
            s->app_mode = true;
            return;
        }
        break;
    case MTA1_MKDF_MMIO_TX_FIFO:
        qemu_chr_fe_write(&s->fifo_chr, &c, 1);
        break;
    case MTA1_MKDF_MMIO_LED:
        s->led = val;
        return;
    case MTA1_MKDF_MMIO_APP_ADDR:
        if (s->app_mode) {
            break;
        } else {
            s->app_addr = val;
            return;
        }
    case MTA1_MKDF_MMIO_APP_SIZE:
        if (s->app_mode) {
            break;
        } else {
            s->app_size = val;
            return;
        }
    }

bad:
    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write: addr=0x%x v=0x%x\n", __func__, (int)addr, (int)val);
}

static uint64_t mta1_mkdf_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    MTA1MKDFState *s = opaque;
    uint8_t r;
    uint64_t val;
    hwaddr end = addr + size;

    /* UDS u8[32] - starts at offset 0 */
    if (end <= MTA1_MKDF_MMIO_UDS + 32) {
        if (s->app_mode) {
            goto bad;
        } else {
            int i = addr - MTA1_MKDF_MMIO_UDS;

            // Should only be read once
            if (s->block_uds[i]) {
                goto bad;
            } else {
                memset(&s->block_uds[i], true, size);
                memcpy(&val, &s->uds[i], size);
                return val;
            }
        }
    }

    /* UDA u8[16] */
    if (addr >= MTA1_MKDF_MMIO_UDA && end <= MTA1_MKDF_MMIO_UDA + 16) {
        if (s->app_mode) {
            goto bad;
        } else {
            memcpy(&val, &s->uda[addr - MTA1_MKDF_MMIO_UDA], size);
            return val;
        }
    }

    /* CDI u8[32] */
    if (addr >= MTA1_MKDF_MMIO_CDI && end <= MTA1_MKDF_MMIO_CDI + 32) {
        memcpy(&val, &s->cdi[addr - MTA1_MKDF_MMIO_CDI], size);
        return val;
    }

    switch (addr) {
    case MTA1_MKDF_MMIO_SWITCH_APP:
        break;
    case MTA1_MKDF_MMIO_UDI:
        return 0xcafebabe;
    case MTA1_MKDF_MMIO_NAME0:
        return 0x6d746131; // "mta1"
    case MTA1_MKDF_MMIO_NAME1:
        return 0x6d6b6466; // "mkdf"
    case MTA1_MKDF_MMIO_VERSION:
        return 1;
    case MTA1_MKDF_MMIO_RX_FIFO_AVAIL:
        return s->fifo_rx_len;
    case MTA1_MKDF_MMIO_RX_FIFO:
        if (s->fifo_rx_len) {
            r = s->fifo_rx[0];
            memmove(s->fifo_rx, s->fifo_rx + 1, s->fifo_rx_len - 1);
            s->fifo_rx_len--;
            qemu_chr_fe_accept_input(&s->fifo_chr);
            return r;
        }
        return 0x80000000;
    case MTA1_MKDF_MMIO_TX_FIFO_AVAIL:
        return 1;
    case MTA1_MKDF_MMIO_TX_FIFO:
        break;
    case MTA1_MKDF_MMIO_LED:
        return s->led;
    case MTA1_MKDF_MMIO_COUNTER: // u32
        break;
    case MTA1_MKDF_MMIO_TRNG_STATUS: // u32
        break;
    case MTA1_MKDF_MMIO_TRNG_DATA: // u32
        break;
    case MTA1_MKDF_MMIO_APP_ADDR:
        return s->app_addr;
    case MTA1_MKDF_MMIO_APP_SIZE:
        return s->app_size;
    }

bad:
    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read: addr=0x%x\n", __func__, (int)addr);
    return 0;
}

static const MemoryRegionOps mta1_mkdf_mmio_ops = {
    .read = mta1_mkdf_mmio_read,
    .write = mta1_mkdf_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
#if 0
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
#endif
};

static void mta1_mkdf_board_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MTA1MKDFState *s = MTA1_MKDF_MACHINE(machine);
    const MemMapEntry *memmap = mta1_mkdf_memmap;
    MemoryRegion *sys_mem = get_system_memory();
    Error *err = NULL;

    // Unique Device Secret
    for (int i = 0; i < 32; i ++) {
        s->block_uds[i] = false;
        s->uds[i] = i;
    }

    // Unique Device Authentication key
    for (int i = 0; i < 16; i ++) {
        s->uda[i] = i;
    }

    if (!mta1_mkdf_setup_chardev(s, &err)) {
        error_report_err(err);
        exit(EXIT_FAILURE);
    }

    if (machine->ram_size != mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s.", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    if (strcmp(machine->cpu_type, TYPE_RISCV_CPU_MULLVAD_PICORV32) != 0) {
        error_report("This board can only be used with a Mullvad PicoRV32 CPU");
        exit(EXIT_FAILURE);
    }

    qemu_chr_fe_set_handlers(&s->fifo_chr, mta1_mkdf_fifo_can_rx, mta1_mkdf_fifo_rx,
                             mta1_mkdf_fifo_event, mta1_mkdf_fifo_be_change, s,
                             NULL, true);

    object_initialize_child(OBJECT(machine), "soc", &s->cpus, TYPE_RISCV_HART_ARRAY);
    object_property_set_str(OBJECT(&s->cpus), "cpu-type", machine->cpu_type, &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "num-harts", machine->smp.cpus, &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "resetvec", memmap[MTA1_MKDF_ROM].base, &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_abort);

    memory_region_init_rom(&s->rom, NULL, "riscv.mta1_mkdf.rom", memmap[MTA1_MKDF_ROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[MTA1_MKDF_ROM].base, &s->rom);

    memory_region_add_subregion(sys_mem, memmap[MTA1_MKDF_RAM].base, machine->ram);

    memory_region_init_io(&s->mmio, OBJECT(s), &mta1_mkdf_mmio_ops, s, "riscv.mta1_mkdf.mmio",
                          memmap[MTA1_MKDF_MMIO].size);
    memory_region_add_subregion(sys_mem, memmap[MTA1_MKDF_MMIO].base, &s->mmio);
    // sysbus_init_mmio(sbd, &s->mmio); // XXX add to sysbusdevice?

    if (!machine->firmware) {
        error_report("No firmware provided! Please use the -bios option.");
        exit(EXIT_FAILURE);
    }

    riscv_load_firmware(machine->firmware, memmap[MTA1_MKDF_RAM].base, htif_symbol_callback);
    htif_mm_init(sys_mem, &s->rom, &s->cpus.harts[0].env, serial_hd(0));
}

static void mta1_mkdf_machine_instance_init(Object *obj)
{
}

static void mta1_mkdf_machine_set_chardev(Object *obj,
                                    const char *value, Error **errp)
{
    MTA1MKDFState *s = MTA1_MKDF_MACHINE(obj);

    g_free(s->fifo_chr_name);
    s->fifo_chr_name = g_strdup(value);
}

static char * mta1_mkdf_machine_get_chardev(Object *obj, Error **errp)
{
    MTA1MKDFState *s = MTA1_MKDF_MACHINE(obj);
    Chardev *chr = qemu_chr_fe_get_driver(&s->fifo_chr);

    if (chr && chr->label) {
        return g_strdup(chr->label);
    }

    return NULL;
}

static void mta1_mkdf_machine_instance_finalize(Object *obj)
{
    MTA1MKDFState *s = MTA1_MKDF_MACHINE(obj);

    qemu_chr_fe_deinit(&s->fifo_chr, false);
    g_free(s->fifo_chr_name);
}

static void mta1_mkdf_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Mullvad MTA1-MKDF Board";
    mc->init = mta1_mkdf_board_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = MULLVAD_PICORV32_CPU;
    mc->default_ram_id = "riscv.mta1_mkdf.ram";
    mc->default_ram_size = mta1_mkdf_memmap[MTA1_MKDF_RAM].size;

    object_class_property_add_str(oc, "fifo",
                                  mta1_mkdf_machine_get_chardev,
                                  mta1_mkdf_machine_set_chardev);

}

static const TypeInfo mta1_mkdf_machine_typeinfo = {
    .name       = TYPE_MTA1_MKDF_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = mta1_mkdf_machine_class_init,
    .instance_init = mta1_mkdf_machine_instance_init,
    .instance_size = sizeof(MTA1MKDFState),
    .instance_finalize = mta1_mkdf_machine_instance_finalize,
};

static void mta1_mkdf_machine_init_register_types(void)
{
    type_register_static(&mta1_mkdf_machine_typeinfo);
}

type_init(mta1_mkdf_machine_init_register_types)
