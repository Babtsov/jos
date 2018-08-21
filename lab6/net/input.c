#include "ns.h"
extern union Nsipc nsipcbuf;

void
input(envid_t ns_envid)
{
	binaryname = "ns_input";

	// LAB 6: Your code here:
	// 	- read a packet from the device driver
	//	- send it to the network server
	// Hint: When you IPC a page to the network server, it will be
	// reading from it for a while, so don't immediately receive
	// another packet in to the same physical page.

	cprintf("ns_input started. envid: %x\n", sys_getenvid());

	// nsipcbuf's page permissions are most likely COW, thus, it is
	// not very convinient to use it as a network buffer. let's get rid of
	// its underlying memory. Because the input environment doesn't need to
	// receive any IPC packets anyway...
	sys_page_unmap(0, &nsipcbuf);

	while(true) {
		sys_page_alloc(0, &nsipcbuf, PTE_U | PTE_W | PTE_P);
		int len;
		while ((len = sys_receive_packet(nsipcbuf.pkt.jp_data,
						 PGSIZE - sizeof(int)))
		       == -E_RX_EMPTY) {
			sys_yield();
		}
		nsipcbuf.pkt.jp_len = len;
		ipc_send(ns_envid, NSREQ_INPUT, &nsipcbuf, PTE_U | PTE_W | PTE_P);

		// remove the page from this env so that it can be mapped only in
		// the destination environment
		sys_page_unmap(0, &nsipcbuf);

	}
}
