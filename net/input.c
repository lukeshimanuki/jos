#include "ns.h"

#include <inc/lib.h>

extern union Nsipc nsipcbuf;

static union Nsipc nsipc __attribute__ ((aligned (PGSIZE)));

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
	while (true) {
		while ((nsipc.pkt.jp_len = sys_receive(nsipc.pkt.jp_data, 2048)) < 0) sys_yield();
		int r;
		cprintf("%x %d\n", &nsipc, nsipc.pkt.jp_len);
		if ((r = sys_ipc_try_send(ns_envid, NSREQ_INPUT, &nsipc, PTE_P|PTE_U|PTE_W)) < 0) panic("input: sys_ipc_try_send");
	}
}
