#include <kern/e1000.h>
#include <kern/pci.h>
#include <kern/pmap.h>

#include <inc/stdio.h>
#include <inc/string.h>
#include <inc/error.h>

// LAB 6: Your driver code here

// Notes:
// The hardware always consumes descriptors from the head and moves the head
// pointer, while the driver always add descriptors to the tail and moves the
// tail pointer.
// Transmit Descriptor Tail register(TDT) is the register that
// holds a value which is an offset from the base, and indicates
// the location beyond the last descriptor hardware can process. This is the
// location where software writes the first new descriptor.

static volatile uint32_t *nic;

#define NIC_REG(offset) (nic[offset / 4])
#define TX_QUEUE_SIZE 64

struct eth_packet_buffer
{
	char data[ETH_MAX_PACKET_SIZE];
};

struct eth_packet_buffer tx_queue_data[TX_QUEUE_SIZE];
struct e1000_tx_desc *tx_queue_desc;

int e1000_attach(struct pci_func *pcif)
{
	pci_func_enable(pcif);
	nic = mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
	cprintf("device status register: %x\n", NIC_REG(E1000_STATUS));

	// Transmit Initialization
	struct PageInfo *p = page_alloc(ALLOC_ZERO);
	physaddr_t tx_queue_base = page2pa(p);
	NIC_REG(E1000_TDBAL) = tx_queue_base;
	NIC_REG(E1000_TDBAH) = 0;
	NIC_REG(E1000_TDLEN) = TX_QUEUE_SIZE * sizeof(struct e1000_tx_desc);
	tx_queue_desc = page2kva(p);
	for (int i = 0; i < TX_QUEUE_SIZE; i++) {
		tx_queue_desc[i].addr = (uint64_t)PADDR(&tx_queue_data[i]);
		// Report Status on, and mark descriptor as end of packet
		tx_queue_desc[i].cmd = (1 << 3) | (1 << 0);
		// set Descriptor Done so we can use this descriptor
		tx_queue_desc[i].status |= E1000_TXD_STAT_DD;
	}
	// initialize head and tail to 0 per documentation
	NIC_REG(E1000_TDH) = 0;
	NIC_REG(E1000_TDT) = 0;

	// control desc page 311
	NIC_REG(E1000_TCTL) |= (E1000_TCTL_EN | E1000_TCTL_PSP);
	NIC_REG(E1000_TCTL) |= E1000_TCTL_COLD & ( 0x40 << 12);  // set the cold
	NIC_REG(E1000_TIPG) = 10; // page 313

	cprintf("tx_queue_base: %x\n", tx_queue_desc);

	cprintf("trying to transmit a packet\n");
	char *data = "Here is some test data to transmit through the NIC";
	tx_packet(data, strlen(data));

	return 0;
}

int tx_packet(char *buf, int size)
{
	assert(size <= ETH_MAX_PACKET_SIZE);
	int tail_indx = NIC_REG(E1000_TDT);
	if (!(tx_queue_desc[tail_indx].status & E1000_TXD_STAT_DD)) {
		return -E_NIC_BUSY; // queue is full
	}
	tx_queue_desc[tail_indx].status &= ~E1000_TXD_STAT_DD;
	memmove(&tx_queue_data[tail_indx].data, buf, size);
	tx_queue_desc[tail_indx].length = size;
	// update the TDT to "submit" this packet for transmission
	NIC_REG(E1000_TDT) = (tail_indx + 1) % TX_QUEUE_SIZE;
	return 0;
}
