/*
 * QEMU RISC-V Board Compatible with Tillitis TK1 platform
 *
 * Copyright (c) 2022 Tillitis AB
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
#include "hw/riscv/tk1.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/misc/unimp.h"
#include "hw/riscv/boot.h"
#include "qemu/units.h"
#include "sysemu/sysemu.h"
#include "hw/riscv/tillitis_cpu.h"
#include "hw/char/riscv_htif.h"
#include "qapi/qmp/qerror.h"
#include "qemu/log.h"
#include "qemu/guest-random.h"
#include "sysemu/runstate.h"

static const MemMapEntry tk1_memmap[] = {
    // TODO js said that currently ROM size is 2048 W32, and max is 3072 W32
    // (8192 and 12288 bytes resp right).
    [TK1_ROM]  = { TK1_ROM_BASE,  0x20000 /*128KB*/ },
    [TK1_RAM]  = { TK1_RAM_BASE,  TK1_RAM_SIZE },
    [TK1_MMIO] = { TK1_MMIO_BASE, TK1_MMIO_SIZE },
};

static bool tk1_setup_chardev(TK1State *s, Error **errp)
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

static void tk1_fifo_rx(void *opaque, const uint8_t *buf, int size)
{
    TK1State *s = opaque;

    if (s->fifo_rx_len >= sizeof(s->fifo_rx)) {
        qemu_log_mask(LOG_GUEST_ERROR, "%s: FIFO Rx dropped! size=%d\n", __func__, size);
        return;
    }
    s->fifo_rx[s->fifo_rx_len++] = *buf;
}

static int tk1_fifo_can_rx(void *opaque)
{
    TK1State *s = opaque;

    return s->fifo_rx_len < sizeof(s->fifo_rx);
}

static void tk1_fifo_event(void *opaque, QEMUChrEvent event)
{
}

static int tk1_fifo_be_change(void *opaque)
{
    TK1State *s = opaque;

    qemu_chr_fe_set_handlers(&s->fifo_chr, tk1_fifo_can_rx, tk1_fifo_rx,
                             tk1_fifo_event, tk1_fifo_be_change, s,
                             NULL, true);

    return 0;
}


static void tk1_mmio_write(void *opaque, hwaddr addr, uint64_t val, unsigned size)
{
    TK1State *s = opaque;
    uint8_t c = val;
    const char *badmsg = "";

    // add base to make absolute
    addr += TK1_MMIO_BASE;

    if (addr == TK1_MMIO_QEMU_DEBUG) {
        putchar(c);
        return;
    }

    // Byte addressable
    if (addr >= TK1_MMIO_FW_RAM_BASE
        && (addr + size) <= (TK1_MMIO_FW_RAM_BASE + TK1_MMIO_FW_RAM_SIZE)) {
        if (s->app_mode) {
            badmsg = "write to FW_RAM in app-mode";
            goto bad;
        }
        memcpy((void *)&s->fw_ram[addr - TK1_MMIO_FW_RAM_BASE], (void *)&val, size);
        return;
    }

    // Check size
    if (size != 4) {
        badmsg = "size not 32 bits";
        goto bad;
    }
    // Check for alignment
    if (addr % 4 != 0) {
        badmsg = "addr not 32-bit aligned";
        goto bad;
    }

    // Handle some read-only addresses first
    if (addr >= TK1_MMIO_UDS_FIRST && addr <= TK1_MMIO_UDS_LAST) {
        badmsg = "write to UDS";
        goto bad;
    }
    if (addr >= TK1_MMIO_TK1_UDI_FIRST && addr <= TK1_MMIO_TK1_UDI_LAST) {
        badmsg = "write to UDI";
        goto bad;
    }

    /* CDI u32[8] */
    if (addr >= TK1_MMIO_TK1_CDI_FIRST && addr <= TK1_MMIO_TK1_CDI_LAST) {
        if (s->app_mode) {
            badmsg = "write to CDI in app-mode";
            goto bad;
        }
        s->cdi[(addr - TK1_MMIO_TK1_CDI_FIRST) / 4] = val;
        return;
    }

    badmsg = "addr/val/state not handled";

    switch (addr) {
    case TK1_MMIO_UART_TX_DATA:
        qemu_chr_fe_write(&s->fifo_chr, &c, 1);
        return;

    case TK1_MMIO_TOUCH_STATUS:
        // Always touched, we don't care about touch reset
        return;

    case TK1_MMIO_TK1_SWITCH_APP:
        if (s->app_mode) {
            badmsg = "write to SWITCH_APP in app-mode";
            break;
        }
        s->app_mode = true;
        return;
    case TK1_MMIO_TK1_LED:
        s->led = val;
        // Not logging LED writes by default, it's too verbose
        // qemu_log_mask(LOG_GUEST_ERROR, "%s: TK1_LED rgb:%c%c%c\n", __func__,
        //               val & (1 << TK1_MMIO_TK1_LED_R_BIT) ? '1' : '0',
        //               val & (1 << TK1_MMIO_TK1_LED_G_BIT) ? '1' : '0',
        //               val & (1 << TK1_MMIO_TK1_LED_B_BIT) ? '1' : '0');
        return;
    case TK1_MMIO_TK1_APP_ADDR:
        if (s->app_mode) {
            badmsg = "write to APP_ADDR in app-mode";
            break;
        }
        s->app_addr = val;
        return;
    case TK1_MMIO_TK1_APP_SIZE:
        if (s->app_mode) {
            badmsg = "write to APP_SIZE in app-mode";
            break;
        }
        s->app_size = val;
        return;
    case TK1_MMIO_TK1_BLAKE2S:
        s->blake2s = val;
        return;
    case TK1_MMIO_TIMER_TIMER:
        if (s->timer_running) {
            badmsg = "write to TIMER_TIMER while timer running";
            break;
        }
        s->timer_initial = val;
        s->timer = val;
        return;
    case TK1_MMIO_TIMER_CTRL:
        // Toggle timer
        if (s->timer_running) {
            // Stop. Reset to initial value.
            s->timer_running = false;
            s->timer = s->timer_initial;
        } else {
            // Start and schedule next tick
            s->timer_running = true;
            timer_mod(s->qtimer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->timer_interval);
        }
        return;
    case TK1_MMIO_TIMER_PRESCALER:
        s->timer_prescaler = val;
        if (s->timer_prescaler == 0) {
            s->timer_interval = NANOSECONDS_PER_SECOND / TK1_CLOCK_FREQ;
        } else {
            s->timer_interval = s->timer_prescaler * NANOSECONDS_PER_SECOND / TK1_CLOCK_FREQ;
        }

        return;

    case TK1_MMIO_WATCHDOG_CTRL:
        if (val & (1 << TK1_MMIO_WATCHDOG_CTRL_START_BIT)) {
            // Start and schedule next tick
            s->watchdog_running = true;
            timer_mod(s->qwatchdog, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->watchdog_initial * NANOSECONDS_PER_SECOND / TK1_CLOCK_FREQ);
        } else if (val & (1 << TK1_MMIO_WATCHDOG_CTRL_STOP_BIT)) {
            s->watchdog_running = false;
        }

        return;

    case TK1_MMIO_WATCHDOG_TIMER_INIT:
        s->watchdog_initial = val;

        return;
    }

bad:
    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad write: addr=0x%x size=%d val=0x%x msg='%s'\n",
                  __func__, (int)addr, size, (int)val, badmsg);
}

static uint64_t tk1_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    TK1State *s = opaque;
    uint8_t r;
    const char *badmsg = "";

    // add base to make absolute
    addr += TK1_MMIO_BASE;

    // Byte addressable
    if (addr >= TK1_MMIO_FW_RAM_BASE
        && (addr + size) <= (TK1_MMIO_FW_RAM_BASE + TK1_MMIO_FW_RAM_SIZE)) {
        if (s->app_mode) {
            badmsg = "read from FW_RAM in app-mode";
            goto bad;
        }
        uint32_t val = 0;
        memcpy((void *)&val, (void *)&s->fw_ram[addr - TK1_MMIO_FW_RAM_BASE], size);
        return val;
    }

    // Check size
    if (size != 4) {
        badmsg = "size not 32 bits";
        goto bad;
    }
    // Check for alignment
    if (addr % 4 != 0) {
        badmsg = "addr not 32-bit aligned";
        goto bad;
    }

    /* UDS 32 bytes */
    if (addr >= TK1_MMIO_UDS_FIRST && addr <= TK1_MMIO_UDS_LAST) {
        if (s->app_mode) {
            badmsg = "read from UDS in app-mode";
            goto bad;
        }
        int i = (addr - TK1_MMIO_UDS_FIRST) / 4;
        // Should only be read once
        if (s->block_uds[i]) {
            badmsg = "read from UDS twice";
            goto bad;
        }
        s->block_uds[i] = true;
        return s->uds[i];
    }

    /* CDI 32 bytes */
    if (addr >= TK1_MMIO_TK1_CDI_FIRST && addr <= TK1_MMIO_TK1_CDI_LAST) {
        return s->cdi[(addr - TK1_MMIO_TK1_CDI_FIRST) / 4];
    }

    /* UDI 8 bytes */
    if (addr >= TK1_MMIO_TK1_UDI_FIRST && addr <= TK1_MMIO_TK1_UDI_LAST) {
        if (s->app_mode) {
            badmsg = "read from UDI in app-mode";
            goto bad;
        }
        return s->udi[(addr - TK1_MMIO_TK1_UDI_FIRST) / 4];
    }

    badmsg = "addr/val/state not handled";

    uint32_t entropy;

    switch (addr) {
    case TK1_MMIO_TRNG_STATUS:
        // Always ready
        return 1 << TK1_MMIO_TRNG_STATUS_READY_BIT;
    case TK1_MMIO_TRNG_ENTROPY:
        qemu_guest_getrandom_nofail(&entropy, sizeof(entropy));
        return entropy;

    case TK1_MMIO_TIMER_TIMER: // u32
        return s->timer;
    case TK1_MMIO_TIMER_PRESCALER:
        return s->timer_prescaler;
    case TK1_MMIO_TIMER_STATUS:
        if (s->timer_running) {
            return 0 << TK1_MMIO_TIMER_STATUS_READY_BIT;
        } else {
            return 1 << TK1_MMIO_TIMER_STATUS_READY_BIT;
        }
        break;

    case TK1_MMIO_UART_RX_STATUS:
        return s->fifo_rx_len;
    case TK1_MMIO_UART_RX_DATA:
        if (s->fifo_rx_len) {
            r = s->fifo_rx[0];
            memmove(s->fifo_rx, s->fifo_rx + 1, s->fifo_rx_len - 1);
            s->fifo_rx_len--;
            qemu_chr_fe_accept_input(&s->fifo_chr);
            return r;
        }
        // TODO what is this returning?!
        return 0x80000000;
    case TK1_MMIO_UART_TX_STATUS:
        return 1;
    case TK1_MMIO_UART_TX_DATA:
        badmsg = "read from TX_DATA";
        break;

    case TK1_MMIO_TOUCH_STATUS:
        // Always touched
        return 1 << TK1_MMIO_TOUCH_STATUS_EVENT_BIT;

    case TK1_MMIO_TK1_NAME0:
        return 0x746b3120; // "tk1 "
    case TK1_MMIO_TK1_NAME1:
        return 0x6d6b6466; // "mkdf"
    case TK1_MMIO_TK1_VERSION:
        return 1;
    case TK1_MMIO_TK1_SWITCH_APP:
        if (s->app_mode) {
            return 0xffffffff;
        }
        return 0;
    case TK1_MMIO_TK1_LED:
        return s->led;
    case TK1_MMIO_TK1_APP_ADDR:
        return s->app_addr;
    case TK1_MMIO_TK1_APP_SIZE:
        return s->app_size;
    case TK1_MMIO_TK1_BLAKE2S:
        return s->blake2s;
    }

bad:
    qemu_log_mask(LOG_GUEST_ERROR, "%s: bad read: addr=0x%x size=%d msg='%s'\n",
                  __func__, (int)addr, size, badmsg);
    return 0;
}

static const MemoryRegionOps tk1_mmio_ops = {
    .read = tk1_mmio_read,
    .write = tk1_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
#if 0
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
#endif
};

static void tk1_timer_tick(void *opaque)
{
    TK1State *s = (TK1State *) opaque;

    if (!s->timer_running) {
        // Timer was turned off. Do not schedule any more ticks.
        return;
    }
    s->timer --;
    if (s->timer == 0) {
        // Timer expired
        s->timer_running = false;
    } else {
        // Schedule next tick
        timer_mod(s->qtimer, qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) + s->timer_interval);
    }
}

static void tk1_watchdog(void *opaque)
{
    TK1State *s = (TK1State *) opaque;

    if (!s->watchdog_running) {
        return;
    }

    printf("watchdog hit!\n");

    // TODO Reset everything! Might be managed automatically if we manage to reset the machine?
    s->timer_initial = 0;
    s->timer = 0;
    s->timer_prescaler = 0;
    s->timer_running = false;
    // Default interval is ~18 MHz, ~55 ns
    s->timer_interval = NANOSECONDS_PER_SECOND / TK1_CLOCK_FREQ;

    s->watchdog_initial = 0x7ffffff;
    s->watchdog_running = false;

    // Back to firmware mode
    s->app_mode = false;

    s->app_addr = 0;
    s->app_size = 0;

    for (int i = 0; i < 8; i ++) {
        s->block_uds[i] = false;
    }

    s->led = 0;
    s->blake2s = 0;

    for (int i = 0; i < 8; i ++) {
        s->cdi[i] = 0;
    }

    // TODO Reset CPU. How?
    printf("Trying reset\n");
    // This does nothing
    //qemu_system_reset(SHUTDOWN_CAUSE_GUEST_RESET);

    // This unfortunately prints "No reset function".
    MachineClass *mc;
    mc = current_machine ? MACHINE_GET_CLASS(current_machine) : NULL;
    if (mc && mc->reset) {
        printf("We have the reset function.\n");
        mc->reset(current_machine);
    } else {
        printf("No reset function.\n");
    }
}

static void tk1_board_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    TK1State *s = TK1_MACHINE(machine);
    const MemMapEntry *memmap = tk1_memmap;
    MemoryRegion *sys_mem = get_system_memory();
    Error *err = NULL;

    // The TK1 timer. Every tick we call tk1_timer_tick().
    s->timer_initial = 0;
    s->timer = 0;
    s->timer_prescaler = 0;
    s->timer_running = false;
    // Default interval is ~18 MHz, ~55 ns
    s->timer_interval = NANOSECONDS_PER_SECOND / TK1_CLOCK_FREQ;
    s->qtimer = timer_new_ns(QEMU_CLOCK_VIRTUAL, tk1_timer_tick, s);

    // The watchdog - when it's running and reaches 0 we call tk1_watchdog()
    s->watchdog_initial = 0x7ffffff;
    s->watchdog_running = false;
    s->qwatchdog = timer_new_ns(QEMU_CLOCK_VIRTUAL, tk1_watchdog, s);

    // Unique Device Secret
    uint32_t uds[8] = {
        0x80808080,
        0x91919191,
        0xa2a2a2a2,
        0xb3b3b3b3,
        0xc4c4c4c4,
        0xd5d5d5d5,
        0xe6e6e6e6,
        0xf7f7f7f7
    };
    memcpy(s->uds, uds, 32);

    for (int i = 0; i < 8; i ++) {
        s->block_uds[i] = false;
    }

    uint32_t udi[2] = {
        0x00010203,
        0x04050607
    };
    memcpy(s->udi, udi, 8);

    if (!tk1_setup_chardev(s, &err)) {
        error_report_err(err);
        exit(EXIT_FAILURE);
    }

    if (machine->ram_size != mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s.", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    if (strcmp(machine->cpu_type, TYPE_RISCV_CPU_TILLITIS_PICORV32) != 0) {
        error_report("This board can only be used with a Tillitis PicoRV32 CPU");
        exit(EXIT_FAILURE);
    }

    qemu_chr_fe_set_handlers(&s->fifo_chr, tk1_fifo_can_rx, tk1_fifo_rx,
                             tk1_fifo_event, tk1_fifo_be_change, s,
                             NULL, true);

    object_initialize_child(OBJECT(machine), "soc", &s->cpus, TYPE_RISCV_HART_ARRAY);
    object_property_set_str(OBJECT(&s->cpus), "cpu-type", machine->cpu_type, &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "num-harts", machine->smp.cpus, &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "resetvec", memmap[TK1_ROM].base, &error_abort);
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_abort);

    memory_region_init_rom(&s->rom, NULL, "riscv.tk1.rom", memmap[TK1_ROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[TK1_ROM].base, &s->rom);

    memory_region_add_subregion(sys_mem, memmap[TK1_RAM].base, machine->ram);

    memory_region_init_io(&s->mmio, OBJECT(s), &tk1_mmio_ops, s, "riscv.tk1.mmio",
                          memmap[TK1_MMIO].size);
    memory_region_add_subregion(sys_mem, memmap[TK1_MMIO].base, &s->mmio);
    // sysbus_init_mmio(sbd, &s->mmio); // XXX add to sysbusdevice?

    if (!machine->firmware) {
        error_report("No firmware provided! Please use the -bios option.");
        exit(EXIT_FAILURE);
    }

    riscv_load_firmware(machine->firmware, memmap[TK1_ROM].base, htif_symbol_callback);
    htif_mm_init(sys_mem, &s->rom, &s->cpus.harts[0].env, serial_hd(0));
}

static void tk1_machine_instance_init(Object *obj)
{
}

static void tk1_machine_set_chardev(Object *obj,
                                    const char *value, Error **errp)
{
    TK1State *s = TK1_MACHINE(obj);

    g_free(s->fifo_chr_name);
    s->fifo_chr_name = g_strdup(value);
}

static char * tk1_machine_get_chardev(Object *obj, Error **errp)
{
    TK1State *s = TK1_MACHINE(obj);
    Chardev *chr = qemu_chr_fe_get_driver(&s->fifo_chr);

    if (chr && chr->label) {
        return g_strdup(chr->label);
    }

    return NULL;
}

static void tk1_machine_instance_finalize(Object *obj)
{
    TK1State *s = TK1_MACHINE(obj);

    qemu_chr_fe_deinit(&s->fifo_chr, false);
    g_free(s->fifo_chr_name);
}

static void tk1_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Tillitis TK1 Board";
    mc->init = tk1_board_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = TILLITIS_PICORV32_CPU;
    mc->default_ram_id = "riscv.tk1.ram";
    mc->default_ram_size = tk1_memmap[TK1_RAM].size;

    object_class_property_add_str(oc, "fifo",
                                  tk1_machine_get_chardev,
                                  tk1_machine_set_chardev);

}

static const TypeInfo tk1_machine_typeinfo = {
    .name       = TYPE_TK1_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = tk1_machine_class_init,
    .instance_init = tk1_machine_instance_init,
    .instance_size = sizeof(TK1State),
    .instance_finalize = tk1_machine_instance_finalize,
};

static void tk1_machine_init_register_types(void)
{
    type_register_static(&tk1_machine_typeinfo);
}

type_init(tk1_machine_init_register_types)
