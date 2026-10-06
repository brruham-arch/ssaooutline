#pragma once
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
static bool gotHook(void* a, void* repl, void** orig) {
    *orig = a;
    uintptr_t tgt = (uintptr_t)a & ~(uintptr_t)1;
    FILE* m = fopen("/proc/self/maps", "r");
    if (!m) return false;
    char line[512]; int n = 0;
    while (fgets(line, sizeof line, m)) {
        if (!strstr(line, "libGTASA.so")) continue;
        unsigned long s, e; char perm[8];
        if (sscanf(line, "%lx-%lx %7s", &s, &e, perm) != 3) continue;
        if (perm[0] != 'r' || perm[2] == 'x') continue;
        int prot = PROT_READ | (perm[1] == 'w' ? PROT_WRITE : 0);
        for (uintptr_t p = s; p + 4 <= e; p += 4) {
            if ((*(uintptr_t*)p & ~(uintptr_t)1) == tgt) {
                uintptr_t pg = p & ~(uintptr_t)4095;
                mprotect((void*)pg, 4096, PROT_READ | PROT_WRITE);
                *(uintptr_t*)p = (uintptr_t)repl;
                mprotect((void*)pg, 4096, prot);
                n++;
            }
        }
    }
    fclose(m);
    return n > 0;
}
