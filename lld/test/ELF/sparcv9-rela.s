# REQUIRES: sparc
# RUN: llvm-mc -filetype=obj -triple=sparcv9 %s -o %t.o
# RUN: ld.lld -shared %t.o -o %t.so
# RUN: llvm-readelf -r %t.so | FileCheck %s

# CHECK: Relocation section '.rela.dyn'
# CHECK: R_SPARC_RELATIVE

.data
.quad local

.hidden local
local:
  .quad 0
