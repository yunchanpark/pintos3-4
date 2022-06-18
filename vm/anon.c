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
void
vm_anon_init (void) {
	/* TODO: Set up the swap_disk. */
	swap_disk = disk_get(1, 1); // slot cnt = 1024 (4MB)
    swap_disk_size = disk_size(swap_disk);
    printf("__debug : size : %d\n", swap_disk_size);
    swap_table.swap_bit = bitmap_create(swap_disk_size / 8);
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
    printf("__debug : 5\n");
	struct anon_page *anon_page = &page->anon;
    
    disk_sector_t idx = anon_page->swap_slot;

    for (int i = 0; i < 8; i++) {
        disk_read(swap_disk, idx + i, kva + (DISK_SECTOR_SIZE * i));
        printf("__debug : 2\n");
    }

    lock_acquire(&swap_table.swap_lock);
    bitmap_reset(swap_table.swap_bit, idx / 8); // set idx to false
    lock_release(&swap_table.swap_lock);
    printf("__debug : 6\n");

    return true;
}

/* Swap out the page by writing contents to the swap disk. */
static bool
anon_swap_out (struct page *page) {
	struct anon_page *anon_page = &page->anon;
    // printf("__debug :: swap_out_addr : %p\n", page->va);
    // 1. 
    if (bitmap_all(swap_table.swap_bit, 0, swap_disk_size / 8))
        PANIC("Swap disk unavailable!");

    // 2. 
    lock_acquire(&swap_table.swap_lock);
    disk_sector_t idx = bitmap_scan_and_flip(swap_table.swap_bit, 0, 1, false);
    if (idx == BITMAP_ERROR)
        return false;
    lock_release(&swap_table.swap_lock);
    page->anon.swap_slot = idx * 8;
    // printf("__debug : 1 : idx : %d\n", idx);

    // 3.
    idx = idx * 8;
    for (int i = 0; i < 8; i++) {
        disk_write(swap_disk, idx + i, page->frame->kva + (DISK_SECTOR_SIZE * i));
        // printf("__debug : 2 : addr : %p\n", page->frame->kva + (DISK_SECTOR_SIZE * i));
    }


    // 4. 
    pml4_clear_page(&thread_current()->pml4, page->va);
    // printf("__debug : 3\n");

    // 5. 
    palloc_free_page(page->frame->kva);
    // printf("__debug : 4\n");

    // // 5. 
    // memset(page->frame->kva, 0, PGSIZE);
    // page->frame->page = NULL;
    // page->frame = NULL;
    // printf("__debug : 4\n");

    // printf("__debug : type : %d\n", VM_TYPE(page->operations->type));
    return true;
}

/* Destroy the anonymous page. PAGE will be freed by the caller. */
static void
anon_destroy (struct page *page) {
	struct anon_page *anon_page = &page->anon;
    lock_acquire(&frame_lock);
    list_remove(&page->frame->f_elem);
    free(page->frame);
    lock_release(&frame_lock);
}
