#include "asm.h"

.section .text
.global _entry
_entry:
        li.d        $t0, 0x9000000000000011   # CA (MAT=1), PLV0, 0x9000 xxxx xxxx xxxx
        csrwr       $t0, LOONGARCH_CSR_DMWIN0
        li.d        $t0, 0x8000000000000001   # SUC (MAT=0), PLV0, 0x8000 xxxx xxxx xxxx
        csrwr       $t0, LOONGARCH_CSR_DMWIN1
        li.d        $t0, 0x0000000000000000
        csrwr       $t0, LOONGARCH_CSR_DMWIN2
        li.d        $t0, 0x0000000000000000
        csrwr       $t0, LOONGARCH_CSR_DMWIN3
        li.d        $t0, 0x0000000000000000
        csrwr       $t0, LOONGARCH_CSR_TLBRENTRY
        la.local    $t0, 0f
        lu52i.d     $t0, $t0, -1792           # enter DMWIN0 (0x9000...) before PG=1
        jirl        $zero, $t0, 0
0:      li.w        $t0, 0xb0                 # PLV=0, IE=0, PG=1
        csrwr       $t0, LOONGARCH_CSR_CRMD
        invtlb      0x0,$zero,$zero
        la          $sp, stack0
        li.d        $a0, 1024*4
        csrrd       $a1, LOONGARCH_CSR_CPUID
        andi        $a1, $a1, 0x1ff
        addi.d      $tp, $a1, 0
        addi.d      $a1, $a1, 1
        mul.d       $a0, $a0, $a1
        add.d       $sp, $sp, $a0
        bl          main
spin:
        b           spin
