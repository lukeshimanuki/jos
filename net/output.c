#include "ns.h"

#include <inc/lib.h>

extern union Nsipc nsipcbuf;

static union Nsipc nsipc __attribute__ ((aligned (PGSIZE)));

void
output(envid_t ns_envid)
{
	binaryname = "ns_output";

	// LAB 6: Your code here:
	// 	- read a packet from the network server
	//	- send the packet to the device driver
	while (true) {
		int r;
		if ((r = sys_ipc_recv(&nsipc)) < 0) panic("output: sys_ipc_recv");

		while ((r = sys_transmit(nsipc.pkt.jp_data, nsipc.pkt.jp_len)) < 0) sys_yield();
	}
}
