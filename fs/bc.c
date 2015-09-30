
#include "fs.h"

// Return the virtual address of this disk block.
void*
diskaddr(uint32_t blockno)
{
	if (blockno == 0 || (super && blockno >= super->s_nblocks))
		panic("bad block number %08x in diskaddr", blockno);
	return (char*) (DISKMAP + blockno * BLKSIZE);
}

// Fault any disk block that is read in to memory by
// loading it from disk.
static void
bc_pgfault(struct UTrapframe *utf)
{
	void *addr = (void *) utf->utf_fault_va;
	uint32_t blockno = ((uint32_t)addr - DISKMAP) / BLKSIZE;
	int r;

	// Check that the fault was within the block cache region
	if (addr < (void*)DISKMAP || addr >= (void*)(DISKMAP + DISKSIZE))
		panic("page fault in FS: eip %08x, va %08x, err %04x",
		      utf->utf_eip, addr, utf->utf_err);

	// Sanity check the block number.
	if (super && blockno >= super->s_nblocks)
		panic("reading non-existent block %08x\n", blockno);

	// Allocate a page in the disk map region, read the contents
	// of the block from the disk into that page.
	// Hint: first round addr to page boundary.
	//
	// LAB 5: you code here:
	void * start_addr = ROUNDDOWN(addr, PGSIZE);
	/*cprintf("fault_va=0x%8x, load disk block no %d to start addr 0x%8x\n",
			addr,
		    blockno,
		    start_addr);*/
	int i=0;
	if ((r=sys_page_alloc(0, start_addr, PTE_P|PTE_U|PTE_W))<0)
		panic("failed to alloc a physical page\n");

	if ((r=ide_read(blockno*BLKSECTS, start_addr, BLKSECTS)!=0))
		panic("failed in read in sect %d, return code %d", i, r);
}


void
bc_init(void)
{
	struct Super super;
	set_pgfault_handler(bc_pgfault);

	// cache the super block by reading it once
	memmove(&super, diskaddr(1), sizeof super);
}

