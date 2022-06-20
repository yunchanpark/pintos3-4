/* anon.c: Implementation of page for non-disk image (a.k.a. anonymous page). */

#include "vm/vm.h"
#include "devices/disk.h"
#include <stdio.h>

/* DO NOT MODIFY BELOW LINE */
static struct disk *swap_disk;
disk_sector_t swap_disk_size;
static bool anon_swap_in (struct page *page, void *kva);
static bool anon_swap_out (struct page *page);
static void anon_destroy (struct page *page);
struct swap_table swap_table;

/* DO NOT MODIFY this struct */
static const struct page_operations anon_ops = {
	.swap_in = anon_swap_in,
	.swap_out = anon_swap_out,
	.destroy = anon_destroy,
	.type = VM_ANON,
};

/* Initialize the data for anonymous pages */
/* Setup the swap disk and swap table(bitmap) */
void
vm_anon_init (void) {
	swap_disk = disk_get(1, 1); 
    swap_disk_size = disk_size(swap_disk);
    swap_table.swap_bit = bitmap_create(swap_disk_size / 8); // page(4kb) = sector(512byte * 8) 
    lock_init(&swap_table.swap_lock); 
}

/* Initialize the file mapping */
bool
anon_initializer (struct page *page, enum vm_type type, void *kva) {
	/* Set up the handler */
	page->operations = &anon_ops;
	struct anon_page *anon_page = &page->anon;
	anon_page->swap_slot= 0; 
    return true;
}


/* Swap in the page by read contents from the swap disk. */
static bool
anon_swap_in (struct page *page, void *kva) {
	struct anon_page *anon_page = &page->anon;
    
    disk_sector_t idx = anon_page->swap_slot;
    for (int i = 0; i < 8; i++) {
        disk_read(swap_disk, (idx * 8) + i, kva + (DISK_SECTOR_SIZE * i));
    }

    lock_acquire(&swap_table.swap_lock);
    bitmap_set_multiple(swap_table.swap_bit, idx, 1, false);
    lock_release(&swap_table.swap_lock);

    return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
    // printf("come on!");
	struct anon_page *anon_page = &page->anon;

    lock_acquire(&swap_table.swap_lock);
    disk_sector_t idx = bitmap_scan_and_flip(swap_table.swap_bit, 0, 1, false);
    lock_release(&swap_table.swap_lock);
    if (idx == BITMAP_ERROR) PANIC("Swap disk unavailable!");
    
    page->anon.swap_slot = idx;

    for (int i = 0; i < 8; i++) {
        disk_write(swap_disk, (idx * 8) + i, page->frame->kva + (DISK_SECTOR_SIZE * i));
    }


    pml4_clear_page(thread_current()->pml4, page->va);
    lock_acquire(&frame_lock);
    list_remove(&page->frame->f_elem);
    palloc_free_page(page->frame->kva);
    lock_release(&frame_lock);

    return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
    lock_acquire(&frame_lock);
    if (page->frame != NULL) {
        list_remove(&page->frame->f_elem);
        free(page->frame);
    }
    lock_release(&frame_lock);
}
