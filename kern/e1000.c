#include <kern/e1000.h>
#include <kern/pmap.h>

volatile uint32_t* e1000;

#define NUM_TX_DESCS 16
#define NUM_RX_DESCS 128
#define TX_PKTSIZE 1518
#define RX_PKTSIZE 2048
static struct e1000_tx_desc* tx_desc;
static struct e1000_rx_desc* rx_desc;
static void* tx_pkt_buffer;
static physaddr_t tx_pkt_buffer_pa;
static void* rx_pkt_buffer;
static physaddr_t rx_pkt_buffer_pa;

// LAB 6: Your driver code here
int e1000_attach(struct pci_func *pcif) {
	pci_func_enable(pcif);

	cprintf("mapping e1000 to %x,%d\n", pcif->reg_base[0], pcif->reg_size[0]);
	e1000 = (uint32_t*)mmio_map_region(pcif->reg_base[0], pcif->reg_size[0]);
	cprintf("e1000 status: %x\n", e1000[E1000_STATUS >> 2]);

	// tx
	struct PageInfo* tx_desc_pg = page_alloc(0);
	size_t tx_desc_size = NUM_TX_DESCS * sizeof(struct e1000_tx_desc);
	tx_desc = mmio_map_region(page2pa(tx_desc_pg), tx_desc_size);

	struct PageInfo* tx_buffer_page;
	size_t tx_buffer_size = NUM_TX_DESCS * TX_PKTSIZE;
	for (int i = 0; i * PGSIZE < tx_buffer_size; ++i) {
		tx_buffer_page = page_alloc(0);
		cprintf("e1000 using page %d as tx buffer\n", page2pa(tx_buffer_page) >> 12);
	}
	tx_pkt_buffer_pa = page2pa(tx_buffer_page);
	tx_pkt_buffer = mmio_map_region(tx_pkt_buffer_pa, tx_buffer_size);

	for (size_t i = 0; i < NUM_TX_DESCS; ++i)
		tx_desc[i].upper.fields.status |= E1000_TXD_STAT_DD;

	e1000[E1000_TDLEN >> 2] = tx_desc_size;
	e1000[E1000_TDBAL >> 2] = page2pa(tx_desc_pg);
	e1000[E1000_TDBAH >> 2] = 0;
	e1000[E1000_TDH >> 2] = 0;
	cprintf("setting TDT\n");
	e1000[E1000_TDT >> 2] = 0;
	cprintf("set TDT\n");
	e1000[E1000_TCTL >> 2] |= E1000_TCTL_PSP;
	uint32_t cold = 0x40000;
	e1000[E1000_TCTL >> 2] &= ~(E1000_TCTL_COLD - cold);
	e1000[E1000_TCTL >> 2] |= cold;
	e1000[E1000_TIPG >> 2] = 10 + (8 << 10) + (8 << 20);
	e1000[E1000_TCTL >> 2] |= E1000_TCTL_EN;

	// rx
	for (int i = 0; i < 128; ++i) e1000[(E1000_MTA >> 2) + i] = 0;

	struct PageInfo* rx_desc_pg = page_alloc(0);
	size_t rx_desc_size = NUM_RX_DESCS * sizeof(struct e1000_rx_desc);
	rx_desc = mmio_map_region(page2pa(rx_desc_pg), rx_desc_size);

	struct PageInfo* rx_buffer_page;
	size_t rx_buffer_size = NUM_RX_DESCS * RX_PKTSIZE;
	for (int i = 0; i * PGSIZE < rx_buffer_size; ++i) {
		rx_buffer_page = page_alloc(0);
		cprintf("e1000 using page %d as rx buffer\n", page2pa(rx_buffer_page) >> 12);
	}
	rx_pkt_buffer_pa = page2pa(rx_buffer_page);
	rx_pkt_buffer = mmio_map_region(rx_pkt_buffer_pa, rx_buffer_size);

	for (size_t i = 0; i < NUM_RX_DESCS; ++i) {
		rx_desc[i].status = 0;
		rx_desc[i].buffer_addr = rx_pkt_buffer_pa + i * RX_PKTSIZE;
		rx_desc[i].length = RX_PKTSIZE;
	}

	e1000[E1000_RA >> 2] = 0x12005452;
	e1000[(E1000_RA >> 2) + 1] = 0x5634;
	e1000[(E1000_RA >> 2) + 1] |= E1000_RAH_AV;

	e1000[E1000_RDLEN >> 2] = rx_desc_size;
	e1000[E1000_RDBAL >> 2] = page2pa(rx_desc_pg);
	e1000[E1000_RDBAH >> 2] = 0;
	e1000[E1000_RDH >> 2] = 1;
	e1000[E1000_RDT >> 2] = 0;
	e1000[E1000_RCTL >> 2] = 0;
	e1000[E1000_RCTL >> 2] |= E1000_RCTL_RDMTS_EIGTH;
	e1000[E1000_RCTL >> 2] |= E1000_RCTL_BAM;
	e1000[E1000_RCTL >> 2] |= E1000_RCTL_SZ_16384;
	//e1000[E1000_RCTL >> 2] |= E1000_RCTL_BSEX;
	e1000[E1000_RCTL >> 2] |= E1000_RCTL_EN;

	return 0;
}

// return 0 if successful
// return 1 if buffer full
int e1000_transmit(void* data, size_t len) {
	cprintf("transmit: %x %d\n", data, len);
	uint32_t tdt = e1000[E1000_TDT >> 2];
	struct e1000_tx_desc* desc = &tx_desc[tdt];
	if (desc->upper.fields.status & E1000_TXD_STAT_DD) {
		cprintf("transmitting: %d\n", tdt);
		for (size_t i = 0; i < len; ++i)
			((char*)(tx_pkt_buffer))[tdt * TX_PKTSIZE + i] = ((char*)(data))[i];
		desc->buffer_addr = tx_pkt_buffer_pa + tdt * TX_PKTSIZE;
		desc->lower.data = 0;
		desc->lower.flags.length = len;
		desc->lower.data |= E1000_TXD_CMD_RS | E1000_TXD_CMD_EOP;
		desc->upper.data = 0;

		e1000[E1000_TDT >> 2] = (tdt + 1) % NUM_TX_DESCS;

		return 0;
	} else {
		cprintf("full: %x %d\n", data, len);
		// full
		return -1;
	}
}

// return number of bytes written if successful
// return 1 if buffer empty
int e1000_receive(void* data, size_t len) {
	//cprintf("receive: %x %d\n", data, len);
	//for (int i = 0; i < NUM_RX_DESCS; ++i) cprintf("%d", rx_desc[i].status & E1000_RXD_STAT_DD ? 1 : 0);
	//cprintf("\n");
	uint32_t rdt = e1000[E1000_RDT >> 2] % NUM_RX_DESCS;
	uint32_t idx = rdt + 1;
	//cprintf("%d\n", rdt);
	struct e1000_rx_desc* desc = &rx_desc[idx];
	if (desc->status & E1000_RXD_STAT_DD) {
		cprintf("receiving: %d\n", idx);
		size_t numbytes = desc->length;
		if (len < numbytes) numbytes = len;
		if (RX_PKTSIZE < numbytes) numbytes = RX_PKTSIZE;
		numbytes -= 4;
		//if (numbytes == 64) numbytes = 52; // terrible hack
		//if (numbytes == 68) numbytes = 64;

		for (size_t i = 0; i < numbytes; ++i)
			((char*)(data))[i] = ((char*)(rx_pkt_buffer))[idx * RX_PKTSIZE + i];

		desc->status = 0;
		e1000[E1000_RDT >> 2] = (rdt + 1) % NUM_RX_DESCS;

		return numbytes;
	} else {
		//cprintf("empty: %x %d\n", data, len);
		// full
		return -1;
	}
}

