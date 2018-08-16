#include "ns.h"
#include <inc/lib.h>

#define ETH_MAX_PACKET_SIZE 1518
extern union Nsipc nsipcbuf;

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	int r;
	while(true) {
		envid_t sender;
		int perm;
		if ((r = ipc_recv(&sender, &nsipcbuf, &perm)) < 0) {
			panic("ipc_recv failed: %e", r);
		}
		if (r != NSREQ_OUTPUT || sender != ns_envid || !(perm & PTE_P)) {
			cprintf("invalid messege from %x:  %x, perm: %x. ignoring.",
				r, sender, perm);
			ipc_send(sender, NRES_INVALID_REQ, NULL, 0);
			sys_page_unmap(0, &nsipcbuf);
			continue;
		}
		struct jif_pkt *pkt = &nsipcbuf.pkt;
		char* pkt_data = pkt->jp_data;

		for (int offset = 0;
		     offset < pkt->jp_len;
		     offset += ETH_MAX_PACKET_SIZE) {
			int tx_size = MIN(pkt->jp_len - offset,
					  ETH_MAX_PACKET_SIZE);
			int err;
			while(true) {
				err = sys_transmit_packet(&pkt_data[offset],
							  tx_size);
				if (err == 0) {
					break;
				} else if (err == -E_NIC_BUSY) {
					sys_yield();
				} else {
					panic("unexpected error: %e", err);
				}
			}
		}
		/* packet transmission complete */
	}
}
