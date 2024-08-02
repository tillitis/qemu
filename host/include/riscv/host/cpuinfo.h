/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Host specific cpu identification for RISC-V.
 */

#ifndef HOST_CPUINFO_H
#define HOST_CPUINFO_H

#define CPUINFO_ALWAYS          (1u << 0)  /* so cpuinfo is nonzero */
#define CPUINFO_ZBA             (1u << 1)
#define CPUINFO_ZBB             (1u << 2)
#define CPUINFO_ZICOND          (1u << 3)

/* Initialized with a constructor. */
extern unsigned cpuinfo;

/*
 * We cannot rely on constructor ordering, so other constructors must
 * use the function interface rather than the variable above.
 */
unsigned cpuinfo_init(void);

#endif /* HOST_CPUINFO_H */
