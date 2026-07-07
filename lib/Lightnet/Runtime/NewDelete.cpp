// Minimal C++ runtime support for a panel build with no Arduino framework underneath (see
// hardware redesign plan §10). Arduino's core normally supplies this (its own new.cpp); avr-libc
// itself has no C++ support beyond the compiler's own language runtime, so a from-scratch build
// needs these ~10 lines wherever `new`/`delete` are used (LightnetPanel/LightnetPanelEdge/
// LightnetPinger all use real `new`/`delete`, not just malloc — see the plan's §10 audit).
//
// Backed directly by avr-libc's malloc()/free() (<stdlib.h>) — no arena, no pool, just the
// standard AVR heap between the end of .bss/.data and the stack.

#include <stdlib.h>

void *operator new(size_t size)
{
    return malloc(size);
}

void *operator new[](size_t size)
{
    return malloc(size);
}

void operator delete(void *ptr)
{
    free(ptr);
}

void operator delete[](void *ptr)
{
    free(ptr);
}

// Sized-deallocation overloads (C++14) — the compiler may emit calls to these instead of the
// unsized forms above; the size isn't needed since free() already knows how much to release.
void operator delete(void *ptr, size_t)
{
    free(ptr);
}

void operator delete[](void *ptr, size_t)
{
    free(ptr);
}
