#ifndef VM_ANON_H
#define VM_ANON_H
#include "vm/vm.h"
#include "bitmap.h"
#include "devices/disk.h"
#include "threads/synch.h"
struct page;
enum vm_type;

/*** team 7 ***/
struct anon_page {
    enum vm_type type; // stack, heap...
    /* todo : add swap slot */
    int swap_slot;
};


struct swap_table {
    struct bitmap *swap_bit;
    struct lock swap_lock;
};

void vm_anon_init (void);
bool anon_initializer (struct page *page, enum vm_type type, void *kva);

#endif
