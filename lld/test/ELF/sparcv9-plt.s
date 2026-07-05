# REQUIRES: sparc
# RUN: llvm-mc --position-independent -filetype=obj -triple=sparcv9 %s -o %t.o
# RUN: ld.lld -shared %t.o -o %t.so
# RUN: llvm-readelf -SW -r %t.so | FileCheck --check-prefix=READELF %s
# RUN: llvm-readobj --section-headers %t.so | FileCheck --check-prefix=SEC %s

# READELF-NOT:  .got.plt
# READELF:      .plt PROGBITS {{[0-9a-f]+}} {{[0-9a-f]+}} {{[0-9a-f]+}} 00 WAX 0 0 256
# READELF-NOT:  .got.plt
# READELF:      Relocation section '.rela.plt'
# READELF:      R_SPARC_JMP_SLOT {{.*}} foo

# SEC:      Name: .rela.plt
# SEC:      Flags [
# SEC-NEXT:   SHF_ALLOC
# SEC-NEXT:   SHF_INFO_LINK
# SEC-NEXT: ]

.text
.globl _start
_start:
  call foo
   nop
