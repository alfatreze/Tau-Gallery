.section .text.start
.globl _start
_start:
  la sp, _stack_top
  call main
  li t0, 0x80000004
  sw zero, 0(t0)
1: j 1b
