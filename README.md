# Lab 2 notes
lab link: https://pdos.csail.mit.edu/6.828/2017/labs/lab2/  
potentially useful ans: http://web.mit.edu/~ternus/Public/Public_old/pmap.c  

## Background
Before configuring the paging system with `mem_init`, we note that the virtual memory subsystem has already been configured in entry.S in the following code:
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

