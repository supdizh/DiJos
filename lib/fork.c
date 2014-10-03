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

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// LAB 4: Your code here.
	if((err & FEC_WR) == 0)
		panic("lib/fork.c/pgfault(): not writing to page fault");
	if((uvpd[PDX(addr)] & PTE_P)==0 || (uvpt[PGNUM(addr)] & PTE_COW) == 0)
		panic("lib/fork.c/pgfault(): not a COW page");
	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.
	r = sys_page_alloc(0, (void*)PFTEMP, PTE_U|PTE_W|PTE_P);
	if(r < 0)
		panic("lib/fork.c/pgfault():sys_page_alloc fail:%e",r);
	//copy the data from oldpage to newpage
	//careful rounddown, addr may be in the mid of one page
	addr = ROUNDDOWN(addr, PGSIZE);
	memmove(PFTEMP, addr, PGSIZE);
	//unmap the old and map the new
	r = sys_page_map(0, PFTEMP, 0, addr, PTE_U|PTE_W|PTE_P);
	if(r < 0)
		panic("lib/fork.c/pgfault():sys_page_map(last) fail:%e",r);
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
	void *addr = (void *)((uint32_t) pn * PGSIZE);
	pte_t pte = uvpt[pn];
	if((pte & PTE_W)>0 || (pte & PTE_COW) > 0){
		r = sys_page_map(0, addr, envid, addr, PTE_U|PTE_P|PTE_COW);
		if(r < 0)
			panic("lib/fork.c/duppage():sys_page_map(child) fail %e\n",r);
		//same for the parent
		r = sys_page_map(0, addr, 0, addr, PTE_U | PTE_P | PTE_COW);
		if(r < 0)
			panic("lib/fork.c/duppage():sys_page_map(parent) fail %e\n",r);
	}else{
		//it is a read-only page
		r = sys_page_map(0, addr, envid, addr, PTE_U | PTE_P);
		if(r < 0)
			panic("lib/fork.c/duppage(): sys_page_map(RO) fail %e\n", r);
	}

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
	set_pgfault_handler(pgfault);

	envid_t envid;
	if((envid = sys_exofork()) < 0)
		panic("lib/fork.c/fork() : %e", envid);

	if(envid == 0){ //child, (mem map finished by now)
		//thisenv is a pointer from user stack
		//so by now it is still the same with the child's parent,fix it
		thisenv = envs + ENVX(sys_getenvid());
		return 0;
	}

	//parent code
	//1, map everything in [UTEXT, UXSTACKTOP-PGSIZE]
	uint32_t addr;
	for(addr = UTEXT; addr <UXSTACKTOP-PGSIZE; addr += PGSIZE){
		if( (uvpd[PDX(addr)] & PTE_P) > 0 &&
			(uvpt[PGNUM(addr)] & PTE_P) > 0 &&
			(uvpt[PGNUM(addr)] & PTE_U) > 0 ){
			duppage(envid, PGNUM(addr));
		}
	}
	//2, fresh page for child exception stack
	int r = sys_page_alloc(envid, (void*)(UXSTACKTOP-PGSIZE), PTE_U|PTE_W|PTE_P);
	if(r<0)
		panic("lib/fork.c/fork():fail when alloc page for UXSTACK %e\n",r);

	//3, set the pagefault handler for child, it haven't call fork
	extern void _pgfault_upcall(void);
	sys_env_set_pgfault_upcall(envid, _pgfault_upcall);

	r = sys_env_set_status(envid, ENV_RUNNABLE);
	if( r < 0)
		panic("lib/fork.c/fork():fail when set child status to RUNNABLE %e\n",r);

	return envid;

}
// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
