#include <kern/e1000.h>
#include <kern/pci.h>
#include <inc/stdio.h>

// LAB 6: Your driver code here
int e1000_attach(struct pci_func *pcif) {
	pci_func_enable(pcif);
	return 0;
}
