// implement fork from user space

#include <inc/string.h>
#include <inc/lib.h>
#include <inc/mmu.h>


//#define _FORK_DEBUG_ 1

// PTE_COW marks copy-on-write page table entries.
// It is one of the bits explicitly allocated to user processes (PTE_AVAIL).
#define PTE_COW		0x800
extern void _pgfault_upcall(void);

static void print_pte(pte_t *ptep);
static void print_page_mapping(envid_t);
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
	pte_t pte;
	pde_t pde;

	// Check that the faulting access was (1) a write, and (2) to a
	// copy-on-write page.  If not, panic.
	// Hint:
	//   Use the read-only page table mappings at uvpt
	//   (see <inc/memlayout.h>).

	// JOS user pgdir layout is strange
	// page directory entries  spaced at PGSIZE after UVPT
	// LAB 4: Your code here.

	// here is my first attempt to user general approach
	// PDX(addr) gives offset of pde in pgdir
	// PTX(addr) gives offset of pte in pgtable
	/*
	pte_t* uvpt;
	pde_t* uvpd = (pde_t *) UVPT;
	uvpt = (pte_t*) PTE_ADDR(uvpd[PDX(addr)]);
	if( !(uvpt[PTX(addr)] & PTE_COW) || !(err & FEC_WR))
	{
		panic("The page fault is not copy-on-write type\n");
	}
	*/
	pte = uvpt[PGNUM(addr)];

#ifdef _FORK_DEBUG_
	cprintf("env: %d fault_va: 0x%8x  pte 0x%8x\n", sys_getenvid(), addr, pte);
#endif

	if((pte & (PTE_U|PTE_P)) != (PTE_U|PTE_P))
		panic("The page table is not accessible\n");

	if( (pte & PTE_COW) != PTE_COW || (err & FEC_WR) != FEC_WR )
		panic("The page fault is not copy-on-write type\n");

	// Allocate a new page, map it at a temporary location (PFTEMP),
	// copy the data from the old page to the new page, then move the new
	// page to the old page's address.
	// Hint:
	//   You should make three system calls.
	//   No need to explicitly delete the old page's mapping.

	// LAB 4: Your code here.

	void * start_addr = ROUNDDOWN(addr,PGSIZE);

	//uint32_t envid = sys_getenvid();

	if( (r=sys_page_alloc(0, (void*)PFTEMP, PTE_W|PTE_P|PTE_U)) <0 )
		panic("failed in sys_page_alloc\n");

	memmove((void*)PFTEMP, start_addr, PGSIZE);

	if( (r=sys_page_map(0, (void*)PFTEMP, 0, start_addr, PTE_W|PTE_P|PTE_U)) < 0)
		panic("failed in sys_page_alloc\n");

	if( (r=sys_page_unmap(0, (void*)PFTEMP)) < 0 )
		panic("failed in unmap temp page\n");

	cprintf("pg_fault fixed\n");
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
	// LAB 4: Your code here.
	int r;
	void * addr = (void *) (pn*PGSIZE);
	pte_t pte = uvpt[pn];
	//cprintf("pte %8x\n", pte);

	if((pte & (PTE_P|PTE_U))!=(PTE_P|PTE_U))
	{
		panic("can not duplicate not accessible page\n");
	}
	uint32_t parent_envid = sys_getenvid();

	int perm = PTE_P|PTE_U;

	if ( (pte&PTE_W) == PTE_W || (pte&PTE_COW) == PTE_COW )
	{
		// change both to PTE_COW type
		perm |= PTE_COW;
		// map child
		if( (r=sys_page_map(0, addr, envid, addr, perm))<0)
			panic("failed in child page mapping\n");
		// change myself to COW too

		if( (r=sys_page_map(0, addr, 0, addr, perm))<0)
			panic("failed in self page mapping\n");
	} else {
		if( (r=sys_page_map(0, addr, envid, addr, perm))<0)
			panic("failed in child page mapping\n");
	}
	//panic("duppage not implemented");
	return 0;
}

static void print_pte(pte_t *ptep)
{
	cprintf("pte %8x", *ptep);
	if (PTE_P|*ptep)
		cprintf("P");
	else
		cprintf("-");

	if (PTE_U|*ptep)
		cprintf("U");
	else
		cprintf("-");

	if (PTE_P|*ptep)
		cprintf("W");
	else
		cprintf("-");
	cprintf("\n");
}

static void print_page_mapping(envid_t envid)
{
	struct Env *e;
	e = (struct Env *)&envs[ENVX(envid)];
	cprintf("print envid input=%d, get=%d\n", envid, e->env_id);
	pte_t * user_pgdir = e->env_pgdir;

	int i=0;
	for(;i<1024; i++)
	{
		if((user_pgdir[i]&(PTE_P|PTE_U)) == (PTE_P|PTE_P))
		{
			print_pte(&user_pgdir[i]);
		}
	}
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
	// panic("fork not implemented");
	envid_t envid;
	uint8_t *addr;
	int r;
	set_pgfault_handler(pgfault);

	// Allocate a new child environment.
	// The kernel will initialize it with a copy of our register state,
	// so that the child will appear to have called sys_exofork() too -
	// except that in the child, this "fake" call to sys_exofork()
	// will return 0 instead of the envid of the child.
	envid = sys_exofork();

	if (envid < 0)
		panic("sys_exofork: %e", envid);
	if (envid == 0) {
		// We're the child.
		// The copied value of the global variable 'thisenv'
		// is no longer valid (it refers to the parent!).
		// Fix it and return 0.
		thisenv = &envs[ENVX(sys_getenvid())];
		return 0;
	}

	//cprintf("number of pages: %d , %d\n", UTOP/(PGSIZE), PGNUM(UTOP));

	size_t pn;
	for(pn=0; pn < PGNUM(UTOP); pn++)
	{
		//pte_t pte = uvpt[i];
		//cprintf("%d: pte1 %8x\n", i, pte);
		//only mapp the accessible address
		if(pn == PGNUM((UXSTACKTOP-PGSIZE))){
			sys_page_alloc(envid, (void *)(UXSTACKTOP-PGSIZE), PTE_W|PTE_U|PTE_P);
			continue;
		}
		// uvpd + pn this address may not be present
		if ((uvpd[PDX(pn<<PGSHIFT)] & (PTE_P|PTE_U)) != (PTE_P|PTE_U))
			continue;

		if( (uvpt[pn] & (PTE_P|PTE_U)) == (PTE_P|PTE_U)){
				//cprintf("i=%d, 0x%8x va_addr=0x%8x\n",pn, &uvpt[pn], pn*4096);
				duppage(envid, pn);
		}
	}

	//print_page_mapping(envid);
	if ((r = sys_env_set_pgfault_upcall(envid,
										thisenv->env_pgfault_upcall)) < 0)
		panic("error in fork(), sys_env_set_pgfault_upcall: %e\n", r);

	cprintf("I am env: %d, forked child env %d\n", sys_getenvid(), envid);
	// Start the child environment running
	if ((r = sys_env_set_status(envid, ENV_RUNNABLE)) < 0)
		panic("sys_env_set_status: %e", r);

	return envid;
}

// Challenge!
int
sfork(void)
{
	panic("sfork not implemented");
	return -E_INVAL;
}
