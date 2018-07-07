# Lab 3 notes
lab link: https://pdos.csail.mit.edu/6.828/2017/labs/lab3/  
x86 rings, Privilege, and protection: https://manybutfinite.com/post/cpu-rings-privilege-and-protection/  

## ELF slightly more in depth
The ELF format has been encountered as part of the bootloader. The bootloader had to parse the kernel (which is an ELF executabe) in order to load it to memory.
### Useful ELF links:
https://medium.com/@MrJamesFisher/understanding-the-elf-4bd60daac571  
https://linux-audit.com/elf-binaries-on-linux-understanding-and-analysis/#the-anatomy-of-an-elf-file  
https://en.wikipedia.org/wiki/Executable_and_Linkable_Format    
### anatomy of an ELF file
ELF contains program-headers (AKA program segments) and program sections. Program-headers are the ones useful for loading. Program sections are using for linking and debugging.
```bash
# read the program haaders (can also use the --segments flag)
readelf --program-headers hello
# read the program sections (can also use the --sections flag)
readelf --section-headers hello
```

### example of actual contents
```bash
vagrant@vagrant-ubuntu-trusty-32:~/jos$ readelf --program-headers obj/user/hello

Elf file type is EXEC (Executable file)
Entry point 0x800020
There are 4 program headers, starting at offset 52

Program Headers:
  Type           Offset   VirtAddr   PhysAddr   FileSiz MemSiz  Flg Align
  LOAD           0x001000 0x00200000 0x00200000 0x043af 0x043af RW  0x1000
  LOAD           0x006020 0x00800020 0x00800020 0x0160d 0x0160d R E 0x1000
  LOAD           0x008000 0x00802000 0x00802000 0x00004 0x00008 RW  0x1000
  GNU_STACK      0x000000 0x00000000 0x00000000 0x00000 0x00000 RWE 0x10

 Section to Segment mapping:
  Segment Sections...
   00     .stab_info .stab .stabstr
   01     .text .rodata
   02     .data .bss
   03
```

## Going from Kernel to user space and back
The control reaches user space once env_pop_tf is executed (and more specifically when the `iret` instruction is reached).
Then once the execution is inside the user program, the control transfers back to the kernel once a system call is made. If we look at the "hello" binary, we can see the disassembly of the `syscall` function where is the last instruction executed before going back to the kernel space:
```
800f69:       cd 30                   int    $0x30
```

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

