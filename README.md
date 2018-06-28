# Lab 2 notes
lab link: https://pdos.csail.mit.edu/6.828/2017/labs/lab2/  
potentially useful ans: http://web.mit.edu/~ternus/Public/Public_old/pmap.c  

## Setup of paging before the code of lab 2
Before configuring the paging system with `mem_init`, we note that the virtual memory subsystem has already been configured in `entry.S` using the following code:
```asm
# Load the physical address of entry_pgdir into cr3.  entry_pgdir
# is defined in entrypgdir.c.
movl    $(RELOC(entry_pgdir)), %eax
movl    %eax, %cr3
# Turn on paging.
movl    %cr0, %eax
orl     $(CR0_PE|CR0_PG|CR0_WP), %eax
movl    %eax, %cr0
```
`entry_pgdir` (the page directory) is defined as:  
```C
__attribute__((__aligned__(PGSIZE)))
pde_t entry_pgdir[NPDENTRIES] = {
        // Map VA's [0, 4MB) to PA's [0, 4MB)
        [0] = ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P,
        // Map VA's [KERNBASE, KERNBASE+4MB) to PA's [0, 4MB)
        [KERNBASE>>PDXSHIFT] = ((uintptr_t)entry_pgtable - KERNBASE) + PTE_P + PTE_W
};
```
so we notice that the current high virtual addresses are from 0xf0000000 to 0xf03fffff and from 0x00000000 to 0x003fffff.
we can confirm the upper limit 0xf03fffff in gdb:
```
(gdb) x/1b 0xf0400000
0xf0400000:     Cannot access memory at address 0xf0400000
(gdb) x/1b 0xf0400000-1
0xf03fffff:     0x00
```
## Summary of how the memory management works in lab 2
As we can see from `entry_pgdir`, we have the first 4 MB of the RAM available for us to use mapped starting from virtual address 0xf0000000 to 0xf03fffff, and that's the region we are going to focus on. (We will not use the virtual address space 0x00000000 to 0x003fffff because this is outside the region that the kernel is linked against (and those two regions map to the same 4 MB of RAM anyway.))  
We'd like to start allocating memory for us to use for dynamicly allocated data structures, but we have several problems:
1) we don't have something like a `malloc` for us to use to "dynamically allocate stuff on the heap". This means we'll need to create our own allocator.
2) The kernel code and data is already present in RAM (and correspondingly, this means that some virtual address space in 0xf0000000 to 0xf03fffff is already in use), and we need to make sure not to overwrite this data by accident.
3) We need to keep track of which addresses have already been allocated, and which ones are free.

All of those problem are addressed by the `boot_alloc` function. This will be our "temporary" memory allocator which we'll use to start allocating data structures dynamically. It is temporary because it can manage only the first 4MB of RAM, but that's ok because that's the only memory region mapped anyway (and we can't use the RAM addresses that has not been mapped to virtual addresses).  

Now that we have a memory allocator, we'll allocate an array of 1024 entries for our page directory `kern_pgdir`. This directory will be the new page directory that is meant to replace the temporary `entry_pgdir`. Notice that this means that memory addresses returned by `boot_alloc` will have to be page aligned (divisible by 4096). This is because the x86 hardware mandates page alignment for the page directory.

We will be using a data structure that is a hybrid between a linked list and an array to keep track of which (physical) page frames are allocated and which ones are free. Each page frame will have its own `PageInfo` struct that will also store some metadata, and those stucts will form a linked list. The amount of page frames we'll be managing depends on how much RAM we have in the computer, so we'll use `boot_alloc` to allocate an array of `PageInfo` proportional to the amount of RAM available, and will call this array `struct PageInfo *pages`. For example, if we have 131072K RAM, and each page is 4K, then we'll need an array of 32,768 PageInfo elements.

The head of the linked list is `struct PageInfo *page_free_list` and what we'll need to do next is to add all the free frames to this linked list. When adding the free page frames, it is important to first add the free page frames from the original 4MB region managed by `boot_alloc` because although all others page frames are marked as "free", we have no way of accessing them as they are not yet mapped to virtual addresses.

Once we are done with this, we will no longer need to use `boot_alloc` in order to allocate memory because `page_free_list` will now contain all the frames in the original 4MB region that were not yet allocated by `boot_alloc`. In order to effectively use `page_free_list`, we'll create a function called `page_alloc` that "mark" a page frame as in use by popping it off the `page_free_list`.   

The way we can access the data stored in a page frame directly is through the `page2kva` macro. This is how `page2kva` works:  
1) let's say we popped a struct PageInfo from the `page_free_list` (let's call it `pp`). How do we know the physical address that `pp` corresponds to? We can calculate it by finding what index `pp` is in the `pages` array and multiply it by the page size: `(pp - pages)*4096`.
2) But the processor can't address physical address directly, how knowing the physical address help us? Each page frame in the original 4MB region is mapped at virtual address located at an offset of 0xf0000000. So, for example, if we want to write to a page frame located in the physical address `0x01234000`, we'll need to tell the processor to write to the virtual address `0xf1234000`. Again, this trick works only for the page frames located in the original 4MB region of memory that has mapped by `entry_pgdir`. 

Then we proceed with mapping the various memory regions as specified in the comments of `mem_init`. One interesting thing to note is that `mem_init` tells us to do is map the (physical) RAM addresses from 0x00000000 - 0x0fffffff into the virtual addresses 0xf0000000-0xffffffff (that's 268 MB, which is more RAM that we might even have!). How much memory overhead this 268MB of address space will create?
Here is how we can calculate it: 268435456 bytes is 65536 page frames (divide by 4096). Each frame occupies 4 bytes in a page table entry. So this means we'll need 65536 * 4 = 262,144 bytes to store all the page tables of these mappings. This looks like it can fit 👍. If want to know how many page tables this will require: 65536 (total frames) / 1024 (frames mapped in each page table) = 64 page tables.

The last thing left for us to do is to load the physical address of `kern_pgdir` into `cr3` register, and our new page directory is installed.

## Exercise 1
### external linker symbol
In the boot_alloc function, we have `extern char end[];`. That thing buffled me on how it works, and I decided to dig into it.  
In the linker script we see: `PROVIDE(end = .);` right after the .bss section. So this is where the linker placed the symbol represented by `end`.  
in order to confirm this, I noticed that gdb was confused by `(char *)end`. See [linker script defined variable](https://stackoverflow.com/questions/8398755/access-symbols-defined-in-the-linker-script-by-application). But we can fix this by casting it to an array type [thanks to this SO answer](https://stackoverflow.com/a/44877599/4862276)  
```
# GDB confused:
(gdb) p (char *)end
$2 = 0xfff10004 <error: Cannot access memory at address 0xfff10004>
(gdb) p &end
$3 = (<data variable, no debug info> *) 0xf0118bd0```

# GDB is consistent with reality:
(gdb) p (char *)(char[1])end
$12 = 0xf0118bd0 "\004"
(gdb) p &(char[1])end
$13 = (char (*)[1]) 0xf0118bd0

# first value of nextfree:
(gdb) p nextfree
$1 = 0xf0119000 ""

value of kernel.asm to confirm:
                extern char end[];
                nextfree = ROUNDUP((char *) end, PGSIZE);
f0100f0f:       c7 45 fc 00 10 00 00    movl   $0x1000,-0x4(%ebp)
f0100f16:       b8 d0 8b 11 f0          mov    $0xf0118bd0,%eax
```
Let's verify the value of `0xf0118bd0` from the executable itself:  
```
vagrant@vagrant-ubuntu-trusty-32:~/jos$ objdump -h obj/kern/kernel | egrep 'Idx|\.data|\.bss'
Idx Name          Size      VMA       LMA       File off  Algn
  4 .data         0000a564  f010e000  0010e000  0000f000  2**12
  5 .bss          00000650  f0118580  00118580  00019564  2**5
vagrant@vagrant-ubuntu-trusty-32:~/jos$ python -c 'print(hex(0xf0118580+0x00000650))'
0xf0118bd0L
```
### Relevant data structures:
The metadata about page frames (physical pages) is stored in:
```C
struct PageInfo {
        // Next page on the free list.
        struct PageInfo *pp_link;
        // pp_ref is the count of pointers (usually in page table entries)
        uint16_t pp_ref;
};
```
and we can allocate an array of these as following (see explanation about boot_alloc in the next section):  
`pages = (struct PageInfo *) boot_alloc(npages * sizeof(struct PageInfo));`  
thus, each page frame (of physical memory) will have a corresponding metadata entry in this array. 
We can use the following helper functions:
```C
// convert from a page to its physical address (use pa2page to do vice versa)
static inline physaddr_t page2pa(struct PageInfo *pp) {
        return (pp - pages) << PGSHIFT;
}
// same as page2pa but returns the kernel virtual address instead.
static inline void* page2kva(struct PageInfo *pp) {
        return KADDR(page2pa(pp));
}
```
### implementing boot_alloc
boot_alloc is a "memory allocator", somewhat similar in its operation to the linux `brk()` system call. except unlike `brk()`, it operates on physical pages (page frames). As already mentioned, the virtual addresses 0xf0000000 to 0xf03fffff are mapped to physical addresses 0x00000000 to 0x003fffff, and this is the address range that `boot_alloc` will be supposed to manage (technically, it will manage all the addresses above the last address used for storing the kenel data. The first such address is 0xf0118bd0). The function signature of `boot_alloc` suggests that we should operate on virtual addresses instead of physical ones, but since there is a one-to-one correspondence betwwen them, it shouldn't get too messy to talk about only virtual addresses (although let's not forget that it's actually physical memory being allocated).  

`boot_alloc` keeps track of what is allocated and what is not allocated with the `static char *nextfree` variable. addresses that are lower than `nextfree` are considered allocated, and addresses equal or above `nextfree` (up to 0xf03fffff)  are considered unallocated. As a consequence, `nextfree` points to the first free address.    
So "Allocating 1 page" means adding 4096 to the address `nextfree` is storing (nextfree += 4096). When we get a request to allocate, say 3 pages, we will store the current value of `nextfree` into `result`, add 3\*4096 to  `nextfree` and will return `result`. this way, the memory region between `result` and `nextfree` is reserved to whomever called `boot_alloc` (notice that in this case, nextfree - result= 3\*4096).  
It is also worth mentioning that the addresses of nextfree and result must be divisible by 4096 (page size). Thus, the first address returned by this function is 0xf0119000).  
```C
static char *nextfree;  // virtual address of next byte of free memory
static void *
boot_alloc(uint32_t n)
{
        char *result;
        // Initialize nextfree if this is the first time.
        // 'end' is a magic symbol automatically generated by the linker,
        // which points to the end of the kernel's bss segment:
        // the first virtual address that the linker did *not* assign
        // to any kernel code or global variables.
        if (!nextfree) {
                extern char end[];
                nextfree = ROUNDUP((char *) end, PGSIZE);
        }
        result = nextfree;

        // Allocate a chunk large enough to hold 'n' bytes, then update
        // nextfree.  Make sure nextfree is kept aligned
        // to a multiple of PGSIZE.
        nextfree = ROUNDUP(result + n, PGSIZE);
        if ((uintptr_t) nextfree >= KERNBASE + PTSIZE) {
                cprintf("boot_alloc: out of memory\n");
                panic("boot_alloc: failed to allocate %d bytes", n);
        }
        return result;
}
```
### implementing page_init
The purpose of this function is to create and initialize a data structure that will keep track of the metadata of all free page frames (physical pages) in the system. A linked list can be a good choice for this because it's very efficient to add to and to remove from. We already have an aray that contains the metadata of all the page frames `struct PageInfo *pages`, so we'll use the elements of this array and add all the free page frames into the linked list represented by `static struct PageInfo *page_free_list`  

How do we know which page frames are free in the extended memory (physical memory above 1M)?  
As previously noted, `boot_alloc` keeps track of all the allocated memory starting from `0xf0119000`, and if we call `boot_alloc(0)`, we'll get the first address which has not been allocated by `boot_alloc`. Because `boot_alloc` returns virtual addresses, and we are interested in the physical address of the page frame, we'll need to use the `PADDR` macro to convert it to the physical address, and then divide it by the page size PGSIZE (4096) to get the appropriate index into the `pages` array.
```C
void
page_init(void)
{
        //  1) Mark physical page 0 as in use.
        //     This way we preserve the real-mode IDT and BIOS structures
        //     in case we ever need them.  (Currently we don't, but...)
        pages[0].pp_ref = 1;
        pages[0].pp_link = NULL;

	//  2) The rest of base memory, [PGSIZE, npages_basemem * PGSIZE)
        //     is free.
        for (int i = 1 ; i < npages_basemem; i++) {
                pages[i].pp_ref = 0;
                pages[i].pp_link = page_free_list;
                page_free_list = &pages[i];
        }

        //  3) Then comes the IO hole [IOPHYSMEM, EXTPHYSMEM), which must
        //     never be allocated.
        uint32_t first_free_pa = (uint32_t) PADDR(boot_alloc(0));
        assert(first_free_pa % PGSIZE == 0);
        int free_pa_pg_indx = first_free_pa / PGSIZE;
        for (int i = npages_basemem ; i < free_pa_pg_indx; i++) {
                pages[i].pp_ref = 1;
                pages[i].pp_link = NULL;
        }

	//  4) Then extended memory [EXTPHYSMEM, ...).
        //     Some of it is in use, some is free. Where is the kernel
        //     in physical memory?  Which pages are already in use for
        //     page tables and other data structures?
        for (int i = free_pa_pg_indx; i < npages; i++) {
                pages[i].pp_ref = 0;
                pages[i].pp_link = page_free_list;
                page_free_list = &pages[i];
        }
}
```
### implementing page_alloc and page_free
The global variable `page_free_list` points to the head of a linked list of the free pages. Therefore, allocating a page means simply popping the head of the linked list, assigning the next element to be the new head, and returning the popped element. Freeing a page means adding an extra element to the linked list. Thus we can implement them the following way:
```C
struct PageInfo *
page_alloc(int alloc_flags)
{
        struct PageInfo* pp = page_free_list;
        if (!pp) {
                return NULL;
        }
        page_free_list = pp->pp_link;
        pp->pp_link = NULL;
        if (alloc_flags & ALLOC_ZERO) {
                memset(page2kva(pp), 0, PGSIZE);
        }
        return pp;
}

//
// Return a page to the free list.
// (This function should only be called when pp->pp_ref reaches 0.)
//
void
page_free(struct PageInfo *pp)
{
        // Hint: You may want to panic if pp->pp_ref is nonzero or
        // pp->pp_link is not NULL.
        assert(pp->pp_ref == 0);
        assert(pp->pp_link == NULL);
        pp->pp_link = page_free_list;
        page_free_list = pp;
}
```
## Exercise 2
Readding material:  
[5.2 Page Translation](https://pdos.csail.mit.edu/6.828/2017/readings/i386/s05_02.htm)  
[6.4 Page-Level Protection](https://pdos.csail.mit.edu/6.828/2017/readings/i386/s06_04.htm)  

## Exercise 3
_Assuming that the following JOS kernel code is correct, what type should variable x have, uintptr_t or physaddr\_t?_
```
	mystery_t x;
	char* value = return_a_pointer();
	*value = 10;
	x = (mystery_t) value;
```
since the function return_a_pointer() returns a virtual address (all pointers are virtual addresses), it should be `uintptr_t`. Then `x` will contain the integer representation of the virtual address of `value`.

## Exercise 4
The following are implementations that seem to work:  
```C
pte_t *
pgdir_walk(pde_t *pgdir, const void *va, int create)
{
        uintptr_t addr = (uintptr_t) va;
        pde_t pde = pgdir[PDX(addr)];
        if (!(pde & PTE_P) && create) {
                struct PageInfo* pd_page = page_alloc(ALLOC_ZERO);
                if (!pd_page) {
                        return NULL;
                }
                pd_page->pp_ref++;
                pde = page2pa(pd_page) | PTE_W | PTE_P | PTE_U;
                pgdir[PDX(addr)] = pde;
        } else if (!(pde & PTE_P)) {
                return NULL;
        }
        physaddr_t pgtable_pa = PTE_ADDR(pde);
        pde_t *pgtable_va = KADDR(pgtable_pa);
        return  &pgtable_va[PTX(addr)];
}

static void
boot_map_region(pde_t *pgdir, uintptr_t va, size_t size, physaddr_t pa, int perm)
{
        assert(size % PGSIZE == 0);
        assert(pa % PGSIZE == 0);
        assert(va % PGSIZE == 0);
        for (int i = 0, n = size / PGSIZE; i < n; i++) {
                pte_t *pte = pgdir_walk(pgdir,(void*) (va + i * PGSIZE), true);
                assert(pte != NULL);
                *pte = (pa + i * PGSIZE) | perm | PTE_P;
        }
}

struct PageInfo *
page_lookup(pde_t *pgdir, void *va, pte_t **pte_store)
{
        // Fill this function in
        pte_t *pte = pgdir_walk(pgdir, va, false);
        if (!pte || !(*pte & PTE_P)) {
                return NULL;
        }
        if (pte_store) {
                *pte_store = pte;
        }
        return pa2page(PTE_ADDR(*pte));
}

void
page_remove(pde_t *pgdir, void *va)
{
        pte_t *pte_store = NULL;
        struct PageInfo *pp = page_lookup(pgdir, va, &pte_store);
        if (!pp) {
                return;
        }
        *pte_store = 0;
        page_decref(pp);
        tlb_invalidate(pgdir, va);
}

int
page_insert(pde_t *pgdir, struct PageInfo *pp, void *va, int perm)
{
        pte_t *pte = pgdir_walk(pgdir, va, true);
        if (!pte) {
                return -E_NO_MEM;
        }
        if (PTE_ADDR(*pte) == page2pa(pp)) {
                if ((*pte & 0x1ff) == perm) {
                        return 0;
                }
                *pte = page2pa(pp) | perm | PTE_P;
                tlb_invalidate(pgdir, va);
                return 0;
        }
        if (*pte & PTE_P) {
                page_remove(pgdir, va);
                assert(*pte == 0);
        }
        pp->pp_ref++;
        *pte = page2pa(pp) | perm | PTE_P;
        return 0;
}
```

## Exercise 5

code added in `mem_init`
```C
//////////////////////////////////////////////////////////////////////
// Allocate an array of npages 'struct PageInfo's and store it in 'pages'.
// The kernel uses this array to keep track of physical pages: for
// each physical page, there is a corresponding struct PageInfo in this
// array.  'npages' is the number of physical pages in memory.  Use memset
// to initialize all fields of each struct PageInfo to 0.

pages = (struct PageInfo *) boot_alloc(npages * sizeof(struct PageInfo));
uintptr_t pages_region_sz = (uintptr_t)boot_alloc(0) - (uintptr_t)pages;
memset(pages, 0, pages_region_sz);

// Map 'pages' read-only by the user at linear address UPAGES
// Permissions:
//    - the new image at UPAGES -- kernel R, user R
//      (ie. perm = PTE_U | PTE_P)
//    - pages itself -- kernel RW, user NONE

boot_map_region(kern_pgdir, UPAGES, PTSIZE, PADDR(pages), PTE_U);

//////////////////////////////////////////////////////////////////////
// Use the physical memory that 'bootstack' refers to as the kernel
// stack.  The kernel stack grows down from virtual address KSTACKTOP.
// We consider the entire range from [KSTACKTOP-PTSIZE, KSTACKTOP)
// to be the kernel stack, but break this into two pieces:
//     * [KSTACKTOP-KSTKSIZE, KSTACKTOP) -- backed by physical memory
//     * [KSTACKTOP-PTSIZE, KSTACKTOP-KSTKSIZE) -- not backed; so if
//       the kernel overflows its stack, it will fault rather than
//       overwrite memory.  Known as a "guard page".
//     Permissions: kernel RW, user NONE

uintptr_t backed_stack = KSTACKTOP-KSTKSIZE;
boot_map_region(kern_pgdir, backed_stack, KSTKSIZE, PADDR(bootstack), PTE_W);


//////////////////////////////////////////////////////////////////////
// Map all of physical memory at KERNBASE.
// Ie.  the VA range [KERNBASE, 2^32) should map to
//      the PA range [0, 2^32 - KERNBASE)
// We might not have 2^32 - KERNBASE bytes of physical memory, but
// we just set up the mapping anyway.
// Permissions: kernel RW, user NONE

uintptr_t pa_end = 0xffffffff - KERNBASE + 1;
boot_map_region(kern_pgdir, KERNBASE, pa_end, 0, PTE_W);

```

### question 2
_What entries (rows) in the page directory have been filled in at this point? What addresses do they map and where do they point? In other words, fill out this table as much as possible:_

We can use the following to perform the relevant calculations:
```python
# To calculate the base virtual address from the offset
hex(int(math.pow(2,22))*offset)
# to calculate the offset from a virtual address:
address // int(math.pow(2,22))
```
We should have the mapping established by mem_init,


Entry | Base Virtual Address  | Points to (logically):
---|---|---
1023|0xffc00000| Page table for top 4MB of phys memory
...|...| page addresses holding RAM
960|0xf0000000| the page table holding the mappings for the beginning of RAM (phyical address 0) (writable)
959|0xefc00000| kernel stack (writable)
958|0xef800000| unmapped
957|0xef400000| a virtual page table at virtual address UVPT.
956|0xef000000|  page table that contains the pages struct (which is readonly)
955|0xeec00000|  unmapped
...|...| unmapped
0|0x00000000| unmapped

### Question 3
_We have placed the kernel and user environment in the same address space. Why will user programs not be able to read or write the kernel's memory? What specific mechanisms protect the kernel memory?_  
They won't be able to do it because pages belonging to the kernel have the PTE_U bit off. This means that if a user program tries to read that page, the processor will generate a page fault, which will transfer control back to the OS.
### Question 4
_What is the maximum amount of physical memory that this operating system can support? Why?_  
Because the OS maps all RAM on the last 268 MB of virtual address space, this will be the maximum amount supported.

### Question 5
_How much space overhead is there for managing memory, if we actually had the maximum amount of physical memory? How is this overhead broken down?_  

If this question means "maximum amount of physical memory" as 4GB, then:  
4294967296 (total RAM) / 4096 (bytes per page) = 1048576 frames.
each frame occupies a page table entry, which is 4 bytes, so total overhead is 4194304 bytes. If we also count the page directory itself, this would be additional 1024\*4 = 4096 bytes. So total overhead is 4,198,400 bytes. This is excluding other data structures allocated by the kernel in order to manage paging.

### Question 6
_Revisit the page table setup in kern/entry.S and kern/entrypgdir.c. Immediately after we turn on paging, EIP is still a low number (a little over 1MB). At what point do we transition to running at an EIP above KERNBASE? What makes it possible for us to continue executing at a low EIP between when we enable paging and when we begin running at an EIP above KERNBASE? Why is this transition necessary?_  
The idea of mapping the first 4MB of the RAM to virtual addresses 0x00000000 and 0xf0000000 is what allows us to execute on both low and high addresses. We transition to running at EIP above KERNBASE when we jump to the C code, which is actually linked at address f0100000.


## Challenge

I implemented the following additional kernel monitor commands (see the source code for full code):
* command to dump contents of a page directory or a page table in a pretty printed way
* command that accepts a virtual address and walks the page directory until it reaches the (physical) page frame, printing the offsets and the virtual addresses of the page directory and the page table, and lastly printing the physical address of the page frame.


## random notes:
generate gcc preprocessed file: `gcc -E pmap.c  -DJOS_KERNEL -I/home/vagrant/jos`  


### Memory map
```
/*
 * Virtual memory map:                                Permissions
 *                                                    kernel/user
 *
 *    4 Gig -------->  +------------------------------+
 *                     |                              | RW/--
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     :              .               :
 *                     :              .               :
 *                     :              .               :
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~| RW/--
 *                     |                              | RW/--
 *                     |   Remapped Physical Memory   | RW/--
 *                     |                              | RW/--
 *    KERNBASE, ---->  +------------------------------+ 0xf0000000      --+
 *    KSTACKTOP        |     CPU0's Kernel Stack      | RW/--  KSTKSIZE   |
 *                     | - - - - - - - - - - - - - - -|                   |
 *                     |      Invalid Memory (*)      | --/--  KSTKGAP    |
 *                     +------------------------------+                   |
 *                     |     CPU1's Kernel Stack      | RW/--  KSTKSIZE   |
 *                     | - - - - - - - - - - - - - - -|                 PTSIZE
 *                     |      Invalid Memory (*)      | --/--  KSTKGAP    |
 *                     +------------------------------+                   |
 *                     :              .               :                   |
 *                     :              .               :                   |
 *    MMIOLIM ------>  +------------------------------+ 0xefc00000      --+
 *                     |       Memory-mapped I/O      | RW/--  PTSIZE
 * ULIM, MMIOBASE -->  +------------------------------+ 0xef800000
 *                     |  Cur. Page Table (User R-)   | R-/R-  PTSIZE
 *    UVPT      ---->  +------------------------------+ 0xef400000
 *                     |          RO PAGES            | R-/R-  PTSIZE
 *    UPAGES    ---->  +------------------------------+ 0xef000000
 *                     |           RO ENVS            | R-/R-  PTSIZE
 * UTOP,UENVS ------>  +------------------------------+ 0xeec00000
 * UXSTACKTOP -/       |     User Exception Stack     | RW/RW  PGSIZE
 *                     +------------------------------+ 0xeebff000
 *                     |       Empty Memory (*)       | --/--  PGSIZE
 *    USTACKTOP  --->  +------------------------------+ 0xeebfe000
 *                     |      Normal User Stack       | RW/RW  PGSIZE
 *                     +------------------------------+ 0xeebfd000
 *                     |                              |
 *                     |                              |
 *                     ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *                     .                              .
 *                     .                              .
 *                     .                              .
 *                     |~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~|
 *                     |     Program Data & Heap      |
 *    UTEXT -------->  +------------------------------+ 0x00800000
 *    PFTEMP ------->  |       Empty Memory (*)       |        PTSIZE
 *                     |                              |
 *    UTEMP -------->  +------------------------------+ 0x00400000      --+
 *                     |       Empty Memory (*)       |                   |
 *                     | - - - - - - - - - - - - - - -|                   |
 *                     |  User STAB Data (optional)   |                 PTSIZE
 *    USTABDATA ---->  +------------------------------+ 0x00200000        |
 *                     |       Empty Memory (*)       |                   |
 *    0 ------------>  +------------------------------+                 --+
 */
 // User read-only virtual page table (see 'uvpt' below)
#define UVPT            (ULIM - PTSIZE)

// Read-only copies of the Page structures
#define UPAGES          (UVPT - PTSIZE)

// Read-only copies of the global env structures
#define UENVS           (UPAGES - PTSIZE)
```
## Other Useful Macro Descriptions
```C
// A linear address 'la' has a three-part structure as follows:
//
// +--------10------+-------10-------+---------12----------+
// | Page Directory |   Page Table   | Offset within Page  |
// |      Index     |      Index     |                     |
// +----------------+----------------+---------------------+
//  \--- PDX(la) --/ \--- PTX(la) --/ \---- PGOFF(la) ----/
//  \---------- PGNUM(la) ----------/
//
// The PDX, PTX, PGOFF, and PGNUM macros decompose linear addresses as shown.
// To construct a linear address la from PDX(la), PTX(la), and PGOFF(la),
// use PGADDR(PDX(la), PTX(la), PGOFF(la)).

// translate an address in kernel space to a physical address (use KADDR to do vice versa)
PADDR(kva) {
  return (physaddr_t)kva - KERNBASE
}
```

