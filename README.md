# Lab 3 notes
lab link: https://pdos.csail.mit.edu/6.828/2017/labs/lab3/  
x86 rings, Privilege, and protection: https://manybutfinite.com/post/cpu-rings-privilege-and-protection/  
x86 assembly reference at&t syntax: https://gist.github.com/mishurov/6bcf04df329973c15044    
x86 assembly reference from Oracle: [here](https://docs.oracle.com/cd/E19253-01/817-5477/6mkuavhs9/index.html#indexterm-162)  

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
Then once the execution is inside the user program, the control transfers back to the kernel once a system call is made. If we look at the "hello" binary (compiled version of `hello.c`), we can see the disassembly of the `syscall` function where is the last instruction executed before going back to the kernel space. This instruction is `int 0$30` as we can see from:
```
  800f69:       cd 30                   int    $0x30
  800f6b:       89 45 e4                mov    %eax,-0x1c(%ebp)
```
Once the control is back in the kernel space, we push all of the user's registers into the the kernel into what is called "trap frame". We do it so we can later resume the the user process from where it was paused.
To illustrate what the trap frame structure contains:  
This is what the trap frame we get the first time a system call is called in the "hello" binary:
```
TRAP frame at 0xefffffbc
  edi  0x00000000
  esi  0x00000000
  ebp  0xeebfde20
  oesp 0xefffffdc
  ebx  0x00000000
  edx  0xeebfde78
  ecx  0x0000000d
  eax  0x00000000
  es   0x----0023
  ds   0x----0023
  trap 0x00000030 System call
  err  0x00000000
  eip  0x00800f6b
  cs   0x----001b
  flag 0x00000096
  esp  0xeebfddd8
  ss   0x----0023
```
now let's confirm these values with gdb.  
we set a breakpoint right before the processor switches back to the kernel (at "int $0x30")  
```
(gdb) b *0x800f69
Breakpoint 1 at 0x800f69
(gdb) c
Continuing.
The target architecture is assumed to be i386
=> 0x800f69:    int    $0x30
(gdb) info registers
eax            0x0      0
ecx            0xd      13
edx            0xeebfde78       -289415560
ebx            0x0      0
esp            0xeebfddd8       0xeebfddd8
ebp            0xeebfde20       0xeebfde20
esi            0x0      0
edi            0x0      0
eip            0x800f69 0x800f69
eflags         0x96     [ PF AF SF ]
cs             0x1b     27
ss             0x23     35
ds             0x23     35
es             0x23     35
fs             0x23     35
gs             0x23     35
```
We see that looks like all registers match exactly except eip. but this makes sense because the trap frame will contain the instruction after int $0x30, which according to hello.asm is indeed equal to 800f6b, which is the value pushed to the trap frame.


## Exercise 4
### Questions
_What is the purpose of having an individual handler function for each exception/interrupt? (i.e., if all exceptions/interrupts were delivered to the same handler, what feature that exists in the current implementation could not be provided?)_  

If we had just one handler for all the exceptions/interrupts, we wouldn't be able to know which exception/interrupt occured because the x86 hardware is not pushing the vector of the interrupt/exception that has triggered the handler to run.

_Did you have to do anything to make the user/softint program behave correctly? The grade script expects it to produce a general protection fault (trap 13), but softint's code says int $14. Why should this produce interrupt vector 13? What happens if the kernel actually allows softint's int $14 instruction to invoke the kernel's page fault handler (which is interrupt vector 14)?_  

The user-space program is not supposed to be able to trigger trap 14 (page fault). We explicitly forbid the user to trigger these CPU-generated interrupts aritificially (using the `int` instruction) by setting dpl to 0 at the interrupt gate. This means that if the user space program artificially issues an interrupt that it is not authorized to, a protection fault will be generated instead (trap 13). 
If the kernel actually allows this interrupt to be triggered by the user space program, the page fault handler would have run. However, a potentially invalid value might be stored in cr2 (the virtual address which caused the fault), and depending on how the page fault handler is implemented, this can potentially cause allocation of pages that the user process is not otherwise authorized to do, or some other unintended kernel bugs that can compromise the security or the stablity of the system.


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

