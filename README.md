# Lab 1 notes
lab link: https://pdos.csail.mit.edu/6.828/2017/labs/lab1/  
guide with useful gdb commands: https://pdos.csail.mit.edu/6.828/2017/labguide.html  
the x command: http://visualgdb.com/gdbreference/commands/x  
solutions link: https://github.com/Clann24/jos  

## Booting notes:
MIT's memory space map
```
+------------------+  <- 0xFFFFFFFF (4GB)
|      32-bit      |
|  memory mapped   |
|     devices      |
|                  |
/\/\/\/\/\/\/\/\/\/\

/\/\/\/\/\/\/\/\/\/\
|                  |
|      Unused      |
|                  |
+------------------+  <- depends on amount of RAM
|                  |
|                  |
| Extended Memory  |
|                  |
|                  |
+------------------+  <- 0x00100000 (1MB)
|     BIOS ROM     |
+------------------+  <- 0x000F0000 (960KB)
|  16-bit devices, |
|  expansion ROMs  |
+------------------+  <- 0x000C0000 (768KB)
|   VGA Display    |
+------------------+  <- 0x000A0000 (640KB)
|                  |
|    Low Memory    |
|                  |
+------------------+  <- 0x00000000

notes:
The 384KB area from 0x000A0000 through 0x000FFFFF was reserved by the hardware.
```
## inside the BIOS
The IBM PC starts executing at physical address 0x000ffff0, which is at the very top of the 64KB area reserved for the ROM BIOS.  
The PC starts executing with CS = 0xf000 and IP = 0xfff0.  
The first instruction to be executed is a jmp instruction, which jumps to the segmented address CS = 0xf000 and IP = 0xe05b -> 0xfe05b, which is still in the BIOS.  
The BIOS loads the first boot sector (512 bytes) to addresses: 0x7c00 through 0x7dff then JMPs CS:IP to 0000:7c00.
our bootloader resides in these 512 bytes, and notice that this is in the `Low Memory` address space.  
## inside the bootloader
if we look at `obj/boot/boot.asm` we see the disassembly of the actual bootloader. The source code for the bootloader itself is in `boot/` directory.  
To step through the assembly code in gdb: `b *0x7c00` (see the visualgdb site), then `c` to hit the breakpoint. then use `si` to step through the instructions.
Notice if we use:
```
(gdb) x/2x 0x7c00
0x7c00:	0xc031fcfa	0xc08ed88e
```
this corresponds to the disassembled:
```
  .code16                     # Assemble for 16-bit mode
  cli                         # Disable interrupts
    7c00:       fa                      cli
  cld                         # String operations increment
    7c01:       fc                      cld

  # Set up the important data segment registers (DS, ES, SS).
  xorw    %ax,%ax             # Segment number zero
    7c02:       31 c0                   xor    %eax,%eax
  movw    %ax,%ds             # -> Data Segment
    7c04:       8e d8                   mov    %eax,%ds
  movw    %ax,%es             # -> Extra Segment
    7c06:       8e c0                   mov    %eax,%es
``` 
notice the order.
