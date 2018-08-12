#include <kern/e1000.h>
#include <kern/pci.h>
#include <kern/pmap.h>
#include <inc/stdio.h>

static volatile uint32_t *nic;

// LAB 6: Your driver code here
int e1000_attach(struct pci_func *pcif) {
	pci_func_enable(pcif);
	nic = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
	cprintf("device status register: %x\n", nic[2]);
	return 0;
}
