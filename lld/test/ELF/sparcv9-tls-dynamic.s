# REQUIRES: sparc
# RUN: llvm-mc -filetype=obj -triple=sparcv9 %s -o %t.o
# RUN: ld.lld -shared %t.o -o %t.so
# RUN: llvm-readelf -r %t.so | FileCheck %s

# CHECK: R_SPARC_TLS_DTPMOD64 0
# CHECK: R_SPARC_TLS_DTPMOD64 {{.*}} a
# CHECK: R_SPARC_TLS_DTPOFF64 {{.*}} a
# CHECK: R_SPARC_TLS_TPOFF64 {{.*}} b
# CHECK: R_SPARC_JMP_SLOT {{.*}} __tls_get_addr

.text
.globl _start
_start:
  sethi %tgd_hi22(a), %i1
  add %i1, %tgd_lo10(a), %i1
  add %i0, %i1, %o0, %tgd_add(a)
  call __tls_get_addr, %tgd_call(a)
   nop

  sethi %tldm_hi22(a), %i2
  add %i2, %tldm_lo10(a), %i2
  add %i0, %i2, %o0, %tldm_add(a)
  call __tls_get_addr, %tldm_call(a)
   nop

  sethi %tie_hi22(b), %i3
  add %i3, %tie_lo10(b), %i3
  ldx [%i0 + %i3], %i3, %tie_ldx(b)
  add %i3, %g7, %i3, %tie_add(b)

.section .tbss,"awT",@nobits
.globl a
a:
  .zero 8

.globl b
b:
  .zero 8
