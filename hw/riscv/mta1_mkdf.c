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

static const MemMapEntry mta1_mkdf_memmap[] = {
    [MTA1_MKDF_ROM] =  {     0x1000, 0x20000 /*128K*/ },
    [MTA1_MKDF_RAM] =  { 0x80000000, 0x20000 },
    [MTA1_MKDF_FIFO] = { 0x90000000, 0xf000 },
};

static void mta1_mkdf_board_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MTA1MKDFState *s = MTA1_MKDF_MACHINE(machine);
    const MemMapEntry *memmap = mta1_mkdf_memmap;
    MemoryRegion *sys_mem = get_system_memory();

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

    object_initialize_child(OBJECT(machine), "soc", &s->cpus, TYPE_RISCV_HART_ARRAY);
    object_property_set_str(OBJECT(&s->cpus), "cpu-type", machine->cpu_type, &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "num-harts", machine->smp.cpus, &error_abort);
    object_property_set_int(OBJECT(&s->cpus), "resetvec", memmap[MTA1_MKDF_ROM].base, &error_abort); // XXX hardcode in cpu?
    sysbus_realize(SYS_BUS_DEVICE(&s->cpus), &error_abort);

    memory_region_init_rom(&s->rom, NULL, "riscv.mta1_mkdf.rom", memmap[MTA1_MKDF_ROM].size, &error_fatal);
    memory_region_add_subregion(sys_mem, memmap[MTA1_MKDF_ROM].base, &s->rom);
    memory_region_add_subregion(sys_mem, memmap[MTA1_MKDF_RAM].base, machine->ram);

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

static void mta1_mkdf_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Mullvad MTA1-MKDF Board";
    mc->init = mta1_mkdf_board_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = MULLVAD_PICORV32_CPU;
    mc->default_ram_id = "riscv.mta1_mkdf.ram";
    mc->default_ram_size = mta1_mkdf_memmap[MTA1_MKDF_RAM].size;
}

static const TypeInfo mta1_mkdf_machine_typeinfo = {
    .name       = TYPE_MTA1_MKDF_MACHINE,
    .parent     = TYPE_MACHINE,
    .class_init = mta1_mkdf_machine_class_init,
    .instance_init = mta1_mkdf_machine_instance_init,
    .instance_size = sizeof(MTA1MKDFState),
};

static void mta1_mkdf_machine_init_register_types(void)
{
    type_register_static(&mta1_mkdf_machine_typeinfo);
}

type_init(mta1_mkdf_machine_init_register_types)
