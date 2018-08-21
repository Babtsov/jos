// Simple command-line kernel monitor useful for
// controlling the kernel and exploring the system interactively.

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/memlayout.h>
#include <inc/assert.h>
#include <inc/x86.h>

#include <kern/console.h>
#include <kern/monitor.h>
#include <kern/kdebug.h>
#include <kern/trap.h>
#include <kern/pmap.h>
#include <kern/env.h>

#define CMDBUF_SIZE	80	// enough for one VGA text line

void
print_pfields(pte_t pte)
{
	if (pte & PTE_COW) cprintf("PTE_COW ");
	if (pte & PTE_G) cprintf("PTE_G ");
	if (pte & PTE_PS) cprintf("PTE_PS ");
	if (pte & PTE_D) cprintf("PTE_D ");
	if (pte & PTE_A) cprintf("PTE_A ");
	if (pte & PTE_PCD) cprintf("PTE_PCD ");
	if (pte & PTE_PWT) cprintf("PTE_PWT ");
	if (pte & PTE_U) cprintf("PTE_U ");
	if (pte & PTE_W) cprintf("PTE_W ");
	if (pte & PTE_P) cprintf("PTE_P ");
}


struct Command {
	const char *name;
	const char *desc;
	// return -1 to force monitor to exit
	int (*func)(int argc, char** argv, struct Trapframe* tf);
};

static struct Command commands[] = {
	{ "q", "Quit the monitor", mon_quit },
	{ "help", "Display this list of commands", mon_help },
	{ "kerninfo", "Display information about the kernel", mon_kerninfo },
	{ "backtrace", "Display stack trace", mon_backtrace },
	{ "vaddrinfo", "Display information about virtual address", mon_vaddrinfo },
	{ "pgdir", "Display the contents of a page directory or a page table", mon_pgdir },
	{ "vminfo", "Display a summary of all the virtual address space", mon_vminfo },
	{"envinfo", "Display information about environments", mon_envinfo}
};

/***** Implementations of basic kernel monitor commands *****/

int
mon_envinfo(int argc, char **argv, struct Trapframe *tf)
{
	if (curenv) {
		cprintf("current env: %x\n", curenv->env_id);
	} else {
		cprintf("current env: null\n");
	}
	char *status_arr[] = {"ENV_FREE", "ENV_DYING", "ENV_RUNNABLE",
			      "ENV_RUNNING", "ENV_NOT_RUNNABLE"};

	for (int i = 0; i < NENV; i++) {
		const volatile struct Env *env = &envs[i];
		if (env->env_status == ENV_FREE) {
			continue;
		}

		char* status = (env->env_status < sizeof(status_arr)) ?
			status_arr[env->env_status] : "(unknown)";
		cprintf("[%x]: parent_id: %x env_pgdir: %x upcall: %x status: %s ",
			env->env_id,
			env->env_parent_id,
			env->env_pgdir,
			(uintptr_t)env->env_pgfault_upcall,
			status);

		uint32_t eip = env->env_tf.tf_eip;
		struct Eipdebuginfo info;
		debuginfo_eip(eip, &info);
		cprintf("eip: %x (%s:%d)\n", eip, info.eip_file, info.eip_line);
	}
	return 0;
}

int
mon_quit(int argc, char **argv, struct Trapframe *tf)
{
	return -1;
}

int
mon_help(int argc, char **argv, struct Trapframe *tf)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(commands); i++)
		cprintf("%s - %s\n", commands[i].name, commands[i].desc);
	return 0;
}

int
mon_kerninfo(int argc, char **argv, struct Trapframe *tf)
{
	extern char _start[], entry[], etext[], edata[], end[];

	cprintf("Special kernel symbols:\n");
	cprintf("  _start                  %08x (phys)\n", _start);
	cprintf("  entry  %08x (virt)  %08x (phys)\n", entry, entry - KERNBASE);
	cprintf("  etext  %08x (virt)  %08x (phys)\n", etext, etext - KERNBASE);
	cprintf("  edata  %08x (virt)  %08x (phys)\n", edata, edata - KERNBASE);
	cprintf("  end    %08x (virt)  %08x (phys)\n", end, end - KERNBASE);
	cprintf("Kernel executable memory footprint: %dKB\n",
		ROUNDUP(end - entry, 1024) / 1024);
	return 0;
}

int
mon_backtrace(int argc, char **argv, struct Trapframe *tf)
{
	uint32_t my_ebp; // this is the frame pointer of mon_trace itself.
	asm volatile("movl %%ebp,%0" : "=r" (my_ebp));
	cprintf("Stack backtrace:\n");
	uint32_t ebp = my_ebp;
	while (ebp != 0) {
		uint32_t eip = *((uint32_t*)ebp + 1);
		cprintf("ebp %08x eip %08x args", ebp, eip);
		for (int i = 2; i < 7; i++) {
			uint32_t arg = *((uint32_t*)ebp + i);
			cprintf(" %08x ", arg);
		}
		cprintf("\n");

		struct Eipdebuginfo info;
		debuginfo_eip(eip, &info);
		uintptr_t offset = eip - info.eip_fn_addr;
		cprintf("\t%s:%d: ", info.eip_file, info.eip_line);
		cprintf("%.*s+%d\n",info.eip_fn_namelen, info.eip_fn_name, offset);

		ebp = *(uint32_t*)ebp;
	}
	return 0;
}

int
extract_hex_addr(uintptr_t *address, const char *s)
{
        const char *removed_prefix = prefix_find(s, "0x");
        const char *addr_str = removed_prefix ? removed_prefix : s;
        if (strlen(addr_str) > 8) {
		return -1;
        }
	char *endptr = NULL;
        long addr = strtol(addr_str, &endptr, 16);
	if (!endptr || *endptr != '\0') {
		return -1;
	}
	assert(address);
	*address = (uintptr_t)addr;
	return 0;
}
void
print_pagetable_entry(pte_t *pte_table, int offset)
{
	pte_t pte = pte_table[offset];
	cprintf("%03x: %08x ",offset, PTE_ADDR(pte));
	if ((pte & 0xEFF) == 0) {
		cprintf("NONE\n");
		return;
	}
	print_pfields(pte);
	cprintf("\n");
}

int
mon_vaddrinfo(int argc, char **argv, struct Trapframe *tf)
{
	if (argc != 2) {
		cprintf("usage: vaddr <virtual address>\n");
		return 0;
	}
	uintptr_t address;
	if (extract_hex_addr(&address, argv[1]) < 0) {
		cprintf("invalid address entered: %s\n",argv[1]);
		return 0;
	}
	uintptr_t page = ROUNDDOWN(address, PGSIZE);
	pde_t *pgdir = KADDR(rcr3());
	cprintf("Page virtual address:\t\t%08x\n", page);
	cprintf("Page dir virtual address:\t%08x\t", pgdir);
	int pdoffset = PDX(address);
	print_pagetable_entry(pgdir, pdoffset);

	pde_t pde = pgdir[pdoffset];
	if (!(pde & PTE_P)) {
		cprintf("Address not found in page directory\n");
		return 0;
	}
	pte_t *pagetable = KADDR(PTE_ADDR(pde));
	cprintf("Page table virtual address:\t%08x\t", pagetable);
	int ptoffset = PTX(address);
	print_pagetable_entry(pagetable, ptoffset);

	pte_t pte = pagetable[ptoffset];
	if (!(pte & PTE_P)) {
                cprintf("Address not found in page table\n");
                return 0;
        }
	cprintf("Page frame address:\t\t%08x\n", PTE_ADDR(pte));
	cprintf("Physical address:\t\t%08x\n", PTE_ADDR(pte) + PGOFF(address));
	return 0;

}
int
mon_pgdir(int argc, char **argv, struct Trapframe *tf)
{
	if (argc < 2 || argc > 4) {
		cprintf("usage: pgdir <address>\n");
		cprintf("       pgdir <address> <offset>\n");
		cprintf("       pgdir <address> <begin offset> <end offset>\n");
		return 0;
	}

	uintptr_t address;
	if (extract_hex_addr(&address, argv[1]) < 0) {
		cprintf("invalid address %s\n", argv[1]);
		return 0;
	} else if (address % PGSIZE != 0) {
		cprintf("Address of pgdir must be paged aligned.\n");
		return 0;
	} else if (!page_lookup((void *) KADDR(rcr3()), (void *)address, NULL)) {
		cprintf("Virtual address %x is not mapped.\n", address);
		return 0;
	}

	if (argc == 4) { // handle pgdir <address> <begin offset> <end offset>
		uint16_t begin = (uint16_t) strtol(argv[2], NULL, 16);
		uint16_t end = (uint16_t) strtol(argv[3], NULL, 16);
		if (begin > end || end >= NPTENTRIES) {
			cprintf("invalid offset(s): begin should be <= end, and\n");
			cprintf("offset(s) should be between 0 and %x\n", NPTENTRIES - 1);
			return 0;
		}
		for (int i = begin; i <= end; i++) {
			print_pagetable_entry((pte_t *)address, i);
		}
		return 0;
	} else if (argc == 3) { // handle pgdir <address> <offset>
		uint16_t offset = (uint16_t) strtol(argv[2], NULL, 16);
		if (offset >= NPTENTRIES) {
			cprintf("invalid offset: %x", offset);
			return 0;
		}
		print_pagetable_entry((pte_t *)address, offset);
		return 0;
	}
	// handle pgdir <address>

	int empty_pte = 0;
	pte_t * pgtable =  (pte_t *)address;
	cprintf("All occupied entries:\n");
	for (int i = 0; i < NPTENTRIES; i++) {
		pte_t pte = pgtable[i];
		if (pte & PTE_P) {
			print_pagetable_entry(pgtable, i);
		} else {
			empty_pte++;
		}
	}
	cprintf("Empty entries count: %d\n", empty_pte);
	cprintf("Occupied entries count: %d\n", NPTENTRIES - empty_pte);

	cprintf("Ranges for free/occupied regions:\n");
	bool prev_occupied = (pgtable[0] & PTE_P) ? true : false;
	int prev = 0;
	for (int i =  1; i < NPTENTRIES; i++) {
		bool current_occuptied = (pgtable[i] & PTE_P) ? true : false;
		if (current_occuptied != prev_occupied) {
			const char *status = prev_occupied ? "occupied" : "free";
			cprintf("[%03x .. %03x] %s\n", prev, i - 1, status);
			prev_occupied = current_occuptied;
			prev = i;
		}
	}
	cprintf("[%03x .. %03x] %s\n", prev, NPTENTRIES - 1, prev_occupied ? "occupied" : "free");
	return 0;
}

int
mon_vminfo(int argc, char **argv, struct Trapframe *tf)
{
	if ( argc > 2) {
		cprintf("usage: vminfo [<pgdir address>]\n");
		return 0;
	}

	pde_t *pgdir = KADDR(rcr3());
	if (argc == 2) {
		uintptr_t address;
		if (extract_hex_addr(&address, argv[1]) < 0) {
			cprintf("invalid address entered: %s\n", argv[1]);
			return 0;
		}
		pgdir = (pde_t *)address;
	}
	cprintf("Using pgdir at: %08x\n", pgdir);

	uintptr_t unmapped_page_count = 0;
	int prev_pfields = 0;
	const uint64_t one = 1;
	for (uint64_t i = 0, prev = 0; i < (one << 32); i += PGSIZE) {
		uintptr_t addr = i, prev_addr = prev;
		pte_t *pte =  pgdir_walk(pgdir, (const void *)addr, false);
		int pfields = pte ? (*pte & PTE_SYSCALL) : 0;

		if (i == 0) {
			prev_pfields = pfields;
		} else if ( i == (one << 32) - PGSIZE) {
			i = (one << 32);
			pfields = ~prev_pfields;
		}
		if (prev_pfields == pfields) {
			continue;
		}

		uintptr_t page_count = (addr - PGSIZE - prev_addr)/PGSIZE + 1;
		cprintf("[ %08x .. %08x] %07d ", prev_addr, addr - PGSIZE, page_count);
		if (prev_pfields == 0) {
			cprintf("<UNMAPPED>\n");
			unmapped_page_count += page_count;
		} else {
			print_pfields(prev_pfields);
			cprintf("\n");
		}
		prev = i;
		prev_pfields = pfields;
	}
	cprintf("Unmapped page count %d\n", unmapped_page_count);
	return 0;
}

/***** Kernel monitor command interpreter *****/

#define WHITESPACE "\t\r\n "
#define MAXARGS 16

static int
runcmd(char *buf, struct Trapframe *tf)
{
	int argc;
	char *argv[MAXARGS];
	int i;

	// Parse the command buffer into whitespace-separated arguments
	argc = 0;
	argv[argc] = 0;
	while (1) {
		// gobble whitespace
		while (*buf && strchr(WHITESPACE, *buf))
			*buf++ = 0;
		if (*buf == 0)
			break;

		// save and scan past next arg
		if (argc == MAXARGS-1) {
			cprintf("Too many arguments (max %d)\n", MAXARGS);
			return 0;
		}
		argv[argc++] = buf;
		while (*buf && !strchr(WHITESPACE, *buf))
			buf++;
	}
	argv[argc] = 0;

	// Lookup and invoke the command
	if (argc == 0)
		return 0;
	for (i = 0; i < ARRAY_SIZE(commands); i++) {
		if (strcmp(argv[0], commands[i].name) == 0)
			return commands[i].func(argc, argv, tf);
	}
	cprintf("Unknown command '%s'\n", argv[0]);
	return 0;
}

void
monitor(struct Trapframe *tf)
{
	char *buf;

	cprintf("Welcome to the JOS kernel monitor!\n");
	cprintf("Type 'help' for a list of commands.\n");

	if (tf != NULL)
		print_trapframe(tf);

	while (1) {
		buf = readline("K> ");
		if (buf != NULL)
			if (runcmd(buf, tf) < 0)
				break;
	}
}
