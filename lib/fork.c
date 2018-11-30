// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800

//
// Custom page fault handler - if faulting page is copy-on-write,
// map in our own private writable copy.
//
static void
pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t err = utf->utf_err;
	int r;

	//cprintf("pgfault: %x\t%x\n", addr, utf->utf_eip);

	// if not present, allocate
	if (!(uvpt[PGNUM(addr)] & PTE_P)) {
		if ((r = sys_page_alloc(0, ROUNDDOWN(addr, PGSIZE), PTE_P|PTE_U|PTE_W)) < 0)
			panic("pgfault: failed to allocate page");
		return;
	}

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).
	if (!((err & FEC_WR) && (uvpt[PGNUM(addr)] & PTE_COW)))
		panic("pgfault: not a write or not cow: %x %x %x %x", err, uvpt[PGNUM(addr)], addr, utf->utf_eip);

	// LAB 4: Your code here.

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.

	// LAB 4: Your code here.
	const envid_t envid = sys_getenvid();
	if ((r = sys_page_alloc(envid, (void*)PFTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		panic("pgfault: sys_page_alloc: %e", r);
	//cprintf("pgfault2: %x\n", *(uintptr_t*)PFTEMP);
	memcpy((void*)PFTEMP, ROUNDDOWN(addr, PGSIZE), PGSIZE);
	if ((r = sys_page_map(envid, (void*)PFTEMP, envid, addr, PTE_P|PTE_U|PTE_W)) < 0)
		panic("pgfault: sys_page_map: %e", r);
	if ((r = sys_page_unmap(envid, (void*)PFTEMP)) < 0)
		panic("pgfault: sys_page_unmap: %e", r);

	//panic("pgfault not implemented");
}

//
// Map our virtual page pn (address pn*PGSIZE) into the target envid
// at the same virtual address.  If the page is writable or copy-on-write,
// the new mapping must be created copy-on-write, and then our mapping must be
// marked copy-on-write as well.  (Exercise: Why do we need to mark ours
// copy-on-write again if it was already copy-on-write at the beginning of
// this function?)
//
// Returns: 0 on success, < 0 on error.
// It is also OK to panic on error.
//
static int
duppage(envid_t envid, unsigned pn)
{
	int r;

	// LAB 4: Your code here.
	pde_t pde = uvpd[pn >> 10];
	if (!(pde & PTE_P)) return -1;
	pte_t pte = uvpt[pn];

	uint32_t perm = pte & PTE_SHARE ? pte & PTE_SYSCALL : 
		(pte & (PTE_W|PTE_COW) ? ((pte & PTE_SYSCALL) & ~PTE_W) | PTE_COW : pte & PTE_SYSCALL);
	//cprintf("fork: %x\t%x\n", pn, perm);
	const envid_t srcEnvid = sys_getenvid();
	void* addr = (void*)(pn * PGSIZE);
	if ((r = sys_page_map(srcEnvid, addr, envid, addr, perm)) < 0) {
		panic("duppage: sys_page_map 1: %e", r);
		return r;
	}
	if ((r = sys_page_map(envid, addr, srcEnvid, addr, perm)) < 0)
		panic("duppage: sys_page_map 2: %e", r);
	return 0;
}

//
// User-level fork with copy-on-write.
// Set up our page fault handler appropriately.
// Create a child.
// Copy our address space and page fault handler setup to the child.
// Then mark the child as runnable and return.
//
// Returns: child's envid to the parent, 0 to the child, < 0 on error.
// It is also OK to panic on error.
//
// Hint:
//   Use uvpd, uvpt, and duppage.
//   Remember to fix "thisenv" in the child process.
//   Neither user exception stack should ever be marked copy-on-write,
//   so you must allocate a new page for the child's user exception stack.
//
envid_t
fork(void)
{
	// LAB 4: Your code here.
	//panic("fork not implemented");
	extern unsigned char end[];
	int r;
	const envid_t envid = sys_getenvid();
	set_pgfault_handler(pgfault);

	envid_t e = sys_exofork();
	if (e < 0) panic("fork: sys_exofork: %e", e);
	if (e == 0) {
		// child
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}
	for (uint32_t pdi = 0; pdi < PDX(UTOP); ++pdi)
		if (uvpd[pdi] & PTE_P)
			for (uint32_t pn = pdi * 1024; pn < (pdi + 1) * 1024; ++pn)
				if ((uvpt[pn] & PTE_P) && (uvpt[pn] & PTE_U) && pn < PGNUM(USTACKTOP))
					duppage(e, pn);

	// Also copy the stack we are currently running on.
	void* addr = (void*)UXSTACKTOP - PGSIZE;
	if ((r = sys_page_alloc(e, addr, PTE_P|PTE_U|PTE_W)) < 0)
		panic("fork: sys_page_alloc: %e", r);
	if ((r = sys_page_map(e, addr, envid, UTEMP, PTE_P|PTE_U|PTE_W)) < 0)
		panic("fork: sys_page_map: %e", r);
	memcpy(UTEMP, addr, PGSIZE);
	if ((r = sys_page_unmap(envid, UTEMP)) < 0)
		panic("fork: sys_page_unmap: %e", r);
	
	extern void _pgfault_upcall(void);
	if ((r = sys_env_set_pgfault_upcall(e, _pgfault_upcall)) < 0)
		panic("fork: sys_env_set_pgfault_upcall: %e", r);
	// Start the child environment running
	if ((r = sys_env_set_status(e, ENV_RUNNABLE)) < 0)
		panic("fork: sys_env_set_status: %e", r);
	return e;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
