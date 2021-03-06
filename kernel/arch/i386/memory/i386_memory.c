/*
 * i386_memory.c: MISSY Microsystem kernel memory space driver
 * Alpha 3.0 rewrite. Provide a dynamic, flexible virtual memory
 * space for the kernel. The kernel's code section, data section,
 * stack, and heap are managed thru this interface
 *
 *  Created on: Aug 24, 2016
 *      Author: Ben Melikant
 */

#include <stddef.h>
#include <stdint.h>
#include <errno.h>

// kernel private includes
#include <include/exception.h>
#include <include/multiboot.h>
#include <include/balloc.h>

// kernel public includes
#include <kernel/tty.h>

// arch includes
#include <i386/include/memory_virt.h>

// definitions
#define KHEAP_START_ADDR 	0xd0000000
#define KHEAP_MAX_ADDR		0xeffff000
#define PAGE_SIZE			0x1000

// kernel break address
extern uint32_t kernel_end;

uint32_t kernel_break = (uint32_t)(&kernel_end);
uint32_t kheap_last_page = KHEAP_START_ADDR - PAGE_SIZE;

// internal variables
void *kheap_base = NULL;

// internal functions
static void kmemory_init_mboot 	(struct multiboot_info *mboot);
static void kmemory_init_mboot2 (struct multiboot2_tag *mboot2);

// int kmemory_initialize (): Initialize the physical and virtual memory manager
// inputs: none
// returns: none (halts on error)
void kmemory_initialize (void *mboot, unsigned int mboot_magic) {

	terminal_printf ("Starting kernel memory manager... ");

	if (mboot_magic == MULTIBOOT_MAGIC)
		kmemory_init_mboot ((struct multiboot_info *) mboot);
	else if (mboot_magic == MULTIBOOT2_MAGIC)
		kmemory_init_mboot2 ((struct multiboot2_tag *) mboot);
}

// int kspace_initialize (): Initialize the kernel's address space
// inputs: none
// returns: 0 on success, -1 on error (sets errno)
void kspace_initialize () {

	// create the start of the kernel heap. Initial heap is 4kb
	kheap_base = balloc_get ();

	if (!kheap_base)
		kernel_panic ("Failed to allocate space for kernel heap");

	// map the page into the kernel's virtual address space
	vmmngr_map_page ((phys_addr) kheap_base, (virt_addr) KHEAP_START_ADDR);

	// expand the initial heap into the virtual address space
	kernel_break = KHEAP_START_ADDR;
	kheap_last_page = KHEAP_START_ADDR;
}

// int kspace_brk (): Expand the kernel heap by setting the break (data segment end)
// inputs: addr - new address of kernel break
// returns: 0 on success, -1 on error (sets errno to ENOMEM)
int kspace_brk (void *addr) {

	uint32_t bp = (uint32_t) addr;

	// if the new address is outside the available heap space, exit
	if (bp > KHEAP_MAX_ADDR) {

		errno = ENOMEM;
		return -1;
	}

	// if the new address is within the last allocated page,
	// just set the break to the new address
	if ((bp &~ 0xfff) == kheap_last_page)
		kernel_break = bp;

	// we are outside our page boundary so
	// time to allocate some new pages
	else if ((bp &~ 0xfff) > kheap_last_page){

		size_t nblocks = (((bp &~ 0xfff) - kheap_last_page) >> 12);

		for (size_t i = 0; i < nblocks; i++) {

			void *kheap_block = balloc_get ();

			if (!kheap_block) {

				errno = ENOMEM;
				return -1;
			}

			kheap_last_page += PAGE_SIZE;

			if (vmmngr_map_page ((phys_addr) kheap_block, (virt_addr) kheap_last_page) == -1) {

				balloc_free (kheap_block);
				errno = ENOMEM;
				return -1;
			}
		}

		// change the break address
		kernel_break = bp;
	}

	return 0;
}

// void *kspace_sbrk (size_t increment): expand the kernel heap by increment bytes
// inputs: increment - number of bytes to expand
// returns: location of old break address
void *kspace_sbrk (size_t increment) {

	void *old_brk = (void *)(kernel_break);

	// use the old kernel break to set the new break
	if (kspace_brk ((void *)(kernel_break+increment)) != 0) {

		errno = ENOMEM;
		return (void *) -1;
	}

	return old_brk;
}

static void kmemory_init_mboot (struct multiboot_info *mboot) {

	// we need to make sure the bootloader provided the memory size
	if (!bootflag_check (mboot->flags, MEMSZ_PRESENT))
		kernel_panic ("The bootloader did not report the size of RAM");

	// compute the memory size from the multiboot structure
	unsigned int memory_sz = (unsigned int)((mboot->memorySzLo+mboot->memorySzHi) * 1024);

	if (balloc_initialize (memory_sz) == 0)
		kernel_panic ("Failed to initialize physical memory manager");

	// this is an x86-based system; we need to process the memory map
	// before initializing the VMM
	if (!mboot->mmap_addr)
		kernel_panic ("Could not find a memory map in the multiboot pointer");

	struct memory_map_inf *mmap_item = (struct memory_map_inf *)(mboot->mmap_addr);
	unsigned int mmap_len = (unsigned int)(mboot->mmap_length);

	size_t i = 0;
	size_t mmap_count = (size_t)(mmap_len / sizeof (struct memory_map_inf));

	while (i < mmap_count) {

		// flag any regions that are not type 'available'
		if (mmap_item->type != 1)
			balloc_init_region ((unsigned int) mmap_item->baseAddr, (unsigned int) mmap_item->length);

		mmap_item = (struct memory_map_inf *)((unsigned int)mmap_item+mmap_item->sz+sizeof(mmap_item->sz));
		i++;
	}

	// the VMM can now be initialized
	if (vmmngr_initialize () == -1)
		kernel_panic ("Failed to initialize virtual memory manager");


	terminal_printf ("Done!\n");
}

void kmemory_init_mboot2 (struct multiboot2_tag *mboot) {

}
