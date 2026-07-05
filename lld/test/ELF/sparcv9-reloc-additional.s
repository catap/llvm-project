# REQUIRES: sparc
# RUN: llvm-mc -filetype=obj -triple=sparcv9 %s -o %t.o
# RUN: ld.lld %t.o --defsym=abs8=0x7f --defsym=abs16=0x1234 \
# RUN:   --defsym=abs32=0x12345678 --defsym=abs64=0x123456789abcdef \
# RUN:   --defsym=small=0x123 --defsym=neg1=0xffffffffffffffff \
# RUN:   -o %t
# RUN: llvm-objdump -s --section=.data %t | FileCheck --check-prefix=HEX %s
# RUN: llvm-objdump -d --no-show-raw-insn %t | FileCheck %s

## R_SPARC_8, R_SPARC_16, R_SPARC_UA16, R_SPARC_32, R_SPARC_UA32,
## R_SPARC_64, and R_SPARC_UA64.
# HEX:      Contents of section .data:
# HEX-NEXT: {{[0-9a-f]+}} 7f123412 34123456 78123456 78012345
# HEX-NEXT: {{[0-9a-f]+}} 6789abcd ef
.data
  .byte abs8
  .half abs16
  .uahalf abs16
  .word abs32
  .uaword abs32
  .xword abs64

## R_SPARC_13, R_SPARC_HIX22, R_SPARC_LOX10, R_SPARC_WDISP16,
## R_SPARC_WDISP22, and R_SPARC_DISP64.
# CHECK-LABEL: section .text:
# CHECK:       mov 0x123, %g1
# CHECK:       sethi 0x0, %g2
# CHECK-NEXT:  xor %g2, -0x1, %g2
# CHECK:       ba,a
# CHECK:       brnz
.text
.globl _start
_start:
  or %g0, small, %g1
  sethi %hix(neg1), %g2
  xor %g2, %lox(neg1), %g2
  ba,a target
  brnz %g1, target
  .xword target - .
target:
  nop
