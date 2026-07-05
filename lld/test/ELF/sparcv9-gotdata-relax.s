# REQUIRES: sparc
# RUN: llvm-mc -filetype=obj -triple=sparcv9 %s -o %t.o
# RUN: ld.lld -shared %t.o -o %t.so
# RUN: llvm-objdump -d --no-show-raw-insn %t.so | FileCheck %s
# RUN: llvm-readelf -r %t.so | FileCheck --check-prefix=REL %s

# CHECK-LABEL: <local_ref>:
# CHECK:       add %l7, %l1, %l2
# CHECK-LABEL: <extern_ref>:
# CHECK:       ldx [%l7+%l1], %l2

# REL:      R_SPARC_GLOB_DAT {{.*}} extern
# REL-NOT:  local

.text
.globl local_ref
.type local_ref,@function
local_ref:
  sethi %gdop_hix22(local), %l1
  or %l1, %gdop_lox10(local), %l1
  ldx [%l7 + %l1], %l2, %gdop(local)
  retl
   nop

.globl extern_ref
.type extern_ref,@function
extern_ref:
  sethi %gdop_hix22(extern), %l1
  or %l1, %gdop_lox10(extern), %l1
  ldx [%l7 + %l1], %l2, %gdop(extern)
  retl
   nop

.data
.hidden local
local:
  .xword 0
