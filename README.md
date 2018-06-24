# Lab 2 notes
lab link: https://pdos.csail.mit.edu/6.828/2017/labs/lab2/  
potentially useful ans: http://web.mit.edu/~ternus/Public/Public_old/pmap.c  

## Memory map
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

PADDR(kva) {
  return (physaddr_t)kva - KERNBASE
}
```
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
In the boot_alloc function, we have `extern char end[];` 
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
## random notes:
generate gcc preprocessed file: `gcc -E pmap.c  -DJOS_KERNEL -I/home/vagrant/jos`  
