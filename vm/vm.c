/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"
#include "userprog/process.h"
#include "vm/anon.h"


/* Initializes the virtual memory subsystem by invoking each subsystem's
 * intialize codes. */
void
vm_init (void) {
	vm_anon_init ();
	vm_file_init ();
#ifdef EFILESYS  /* For project 4 */
	pagecache_init ();
#endif
	register_inspect_intr ();
	/* DO NOT MODIFY UPPER LINES. */
	/* TODO: Your code goes here. */
    /* team 7 */
    list_init(&frame_list);
    keep = list_head(&frame_list);
    lock_init(&frame_lock);
}

/*** team 7 : for hash ***/
/* Returns a hash value for page p. */
unsigned
page_hash (const struct hash_elem *p_, void *aux UNUSED) {
    const struct page *p = hash_entry (p_, struct page, spt_elem);
    return hash_bytes (&p->va, sizeof p->va);
}

/* Returns true if page a precedes page b. */
bool
page_less (const struct hash_elem *a_, const struct hash_elem *b_, void *aux UNUSED) {
    const struct page *a = hash_entry (a_, struct page, spt_elem);
    const struct page *b = hash_entry (b_, struct page, spt_elem);

    return a->va < b->va;
}

/* use as destructor */
void 
page_destructor (struct hash_elem *h_elem, void *aux) {
    struct page *page = hash_entry(h_elem, struct page, spt_elem);
    vm_dealloc_page(page);
}


/* Get the type of the page. This function is useful if you want to know the
 * type of the page after it will be initialized.
 * This function is fully implemented now. */
enum vm_type
page_get_type (struct page *page) {
	int ty = VM_TYPE (page->operations->type);
	switch (ty) {
		case VM_UNINIT:
			return VM_TYPE (page->uninit.type);
		default:
			return ty;
	}
}

/* Helpers */
static struct frame *vm_get_victim (void);
static bool vm_do_claim_page (struct page *page);
static struct frame *vm_evict_frame (void);

/* Create the pending page object with initializer. If you want to create a
 * page, do not create it directly and make it through this function or
 * `vm_alloc_page`. */
/*** team 7 ***/
bool
vm_alloc_page_with_initializer (enum vm_type type, void *upage, bool writable,
		vm_initializer *init, void *aux) {
	ASSERT (VM_TYPE(type) != VM_UNINIT)

	struct supplemental_page_table *spt = &thread_current ()->spt;
    bool (*initializer) (struct page *, enum vm_type, void *kva); /*** debugging ddalgui ***/
	/* Check wheter the upage is already occupied or not. */
	if (spt_find_page (spt, upage) == NULL) {
        struct page *page = calloc(1, sizeof(struct page));
        ASSERT (page != NULL);

        switch (VM_TYPE(type)) {
            case VM_ANON :
                initializer = anon_initializer;
                break;
            case VM_FILE :
                initializer = file_backed_initializer;
                break;
            default :
                goto err;                
        }
        upage = pg_round_down(upage);
        uninit_new(page, upage, init, type, aux, initializer); /*** debugging ddalgui : type? marker? ***/
        // printf("type: %d\n", type);
        page->writable = writable; /*** debugging ddalgui ***/
        return spt_insert_page(spt, page); /* 변경 */
        
        // if (!condition)
        //     goto err;
            
        // return true;
	}
err:
	return false;
}

/* Find VA from spt and return page. On error, return NULL. */
/*** team : 7 ***/
struct page *
spt_find_page (struct supplemental_page_table *spt UNUSED, void *va UNUSED) {
    struct page page; // fake page
    page.va = pg_round_down(va);

    struct hash_elem *search = hash_find(spt->spt_hash, &page.spt_elem);
    if(search != NULL) {
        return hash_entry(search, struct page, spt_elem);
    }

    return NULL;
}

/* Insert PAGE into spt with validation. */
/*** team 7 ***/
bool
spt_insert_page (struct supplemental_page_table *spt UNUSED, struct page *page UNUSED) {
	int succ = false;
    if (spt == NULL || spt == NULL) return false;
	if(hash_insert(spt->spt_hash, &page->spt_elem) == NULL) {
        succ = true;
    }
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
    hash_delete(spt->spt_hash, &page->spt_elem);
	vm_dealloc_page (page);
}

/* Get the struct frame, that will be evicted. */
/* eviction policy : clock algorithm 
 * 1. 마지막 탐색 지점을 저장할 keep을 전역으로 선언해 참조한다
 * 2. keep에 저장된 위치부터 list의 끝, list의 시작부터 keep까지 탐색한다. (ring)
 * 3. 만약 참조 위치의 access bit이 0이라면 victim으로 정한다. keep은 victim의 next로 옮긴다.
 * 4. 만약 참조 위치의 access bit이 1이라면 bit을 0으로 바꾸고 계속 탐색을 진행한다.
 * 5. 이 알고리즘은 오래된 프레임의 참조 가능성이 낮다고 전제한다. (FIFO) */
static struct frame *
vm_get_victim (void) {
	struct frame *victim;
    struct thread *curr = thread_current();
    struct list_elem *clock_point = keep; 

    // keep이 list의 head인 것에 대비 (head에서 참조할 상위 struct는 frame이 아님)
    if (keep == list_head(&frame_list))
        keep = list_next(keep);

    // 탐색 1 : keep -> end
    for (keep = list_next(keep); keep != list_end(&frame_list); keep = list_next(keep)) {
        victim = list_entry(keep, struct frame, f_elem);
   
        if (!pml4_is_accessed(curr->pml4, victim->page->va)) {
            keep = list_next(keep);
            return victim;
        } else
            pml4_set_accessed(curr->pml4, victim->page->va, 0);
    }
    
    // 탐색 2 : begin -> prev(keep)
    for (keep = list_begin(&frame_list); keep != clock_point; keep = list_next(keep)) {
        victim = list_entry(keep, struct frame, f_elem);
        /* 최근 사용된 적이 없다면 축출*/
        if (!pml4_is_accessed(curr->pml4, victim->page->va)) {
            keep = list_next(keep);
            return victim;
        } else
            pml4_set_accessed(curr->pml4, victim->page->va, 0);
    }

    return NULL;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	return swap_out(victim->page) ? victim : NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This alw`ays return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
static struct frame *
vm_get_frame (void) {
    struct frame *frame = calloc(1, sizeof(struct frame));
    void *temp = palloc_get_page(PAL_ZERO | PAL_USER); // new addr
    
    /* swap out and get a page (retry) */
    if(!temp) {
        // evict
        struct frame *del = vm_evict_frame();
        ASSERT(del != NULL); // swap slot이 꽉 찬 경우는 kernel panic으로, 이 단계에서는 고려하지 않음
        del->page->frame = NULL;
        free(del);

        // 재할당
        temp = palloc_get_page(PAL_ZERO | PAL_USER); 
        // ASSERT (temp != NULL)
    }

    frame->kva = temp;
    ASSERT (frame->kva);
    lock_acquire(&frame_lock);
    list_push_front(&frame_list, &frame->f_elem);
    lock_release(&frame_lock);

    return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
    void *pg_down_addr = pg_round_down(addr);
    if (pg_down_addr >= (void *)(USER_STACK  - (1 << 20)))
        vm_alloc_page(VM_MARKER_0 | VM_ANON, pg_down_addr, 1); // stack은 lazy load하지 않음
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
    /* extra */
}

/* Return true on success */
/* check cases : stack growth, lazy load */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {

    if(!not_present) // write to r/o
        exit(-1);

	struct page *page = NULL;
    struct thread *curr = thread_current();
	struct supplemental_page_table *spt UNUSED = &curr->spt;

    /* rsp check : user인 경우 if에서, kernel인 경우 TCB에서 참조 */
    void *s_rsp = (void *)(user ? f->rsp : curr->vm_rsp);

    /* page fault caused by stack */ 
    if (s_rsp - addr == 0x8 || ((void *)USER_STACK > addr) && (addr > s_rsp))
        vm_stack_growth(addr);

    page = spt_find_page(spt, addr);

    return page == NULL ? false : vm_do_claim_page (page);
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
/* get page from supplemental page table */ 
bool
vm_claim_page (void *va UNUSED) {
	struct page *page = NULL;
    
    page = spt_find_page(&thread_current()->spt, va);
    if (!page) 
        return false;
	return vm_do_claim_page (page);
}

/* Claim the PAGE and set up the mmu. */
static bool
vm_do_claim_page (struct page *page) {
	struct frame *frame = vm_get_frame (); // claim a frame
    if (!frame) 
        return false;

    struct thread *t = thread_current ();

    /* frame - page link */
	frame->page = page;
	page->frame = frame;

	if (pml4_get_page (t->pml4, page->va) == NULL // pml4에 upage unmapping 상태이면
			&& pml4_set_page (t->pml4, page->va, frame->kva, page->writable)) // set pte
        return swap_in (page, frame->kva);

    return false;
}

/* Initialize new supplemental page table */
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
    /* allocate hash structure */
    spt->spt_hash = (struct hash *)calloc(1, sizeof(struct hash));
    hash_init(spt->spt_hash, page_hash, page_less, NULL);
}

/* Free the resource hold by the supplemental page table 
 * Destroy all the supplemental_page_table hold by thread and
 * writeback all the modified contents to the storage. */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
    if (spt->spt_hash) 
        hash_destroy(spt->spt_hash, page_destructor); // call detroy func for each type
}

/* copy the source supplemental page table to destination */
/* 1. set hash iterator
 * 2. spt를 순회하면서 각 페이지 타입에 맞는 copy 과정 수행 */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
    
    struct hash_iterator iter;
    hash_first (&iter, src->spt_hash);
    while (hash_next (&iter)) {      
        struct page *src_p = hash_entry (hash_cur (&iter), struct page, spt_elem);
        switch (src_p->operations->type) {
        case VM_ANON: {
            if(VM_MARKER(src_p->uninit.type)) {
                if(!vm_alloc_page_with_initializer(page_get_type(src_p), src_p->va, src_p->writable, src_p->uninit.init, NULL))
                    goto err;
        
            } else {
                if(!vm_alloc_page_with_initializer(page_get_type(src_p), src_p->va, src_p->writable, NULL, NULL))
                    goto err;
            }
            struct page *dst_p = spt_find_page(dst, src_p->va);
            if (dst_p == NULL)
                goto err;
            if (!vm_do_claim_page(dst_p)) 
                goto err;
            memcpy(dst_p->frame->kva, src_p->frame->kva, PGSIZE);
            break;
        }
        case VM_FILE: {
            struct file_page *file_page = &src_p->file; 
            struct lazy_file *lazy_file = calloc(1, sizeof(struct lazy_file));
            lazy_file->ofs = file_page->ofs;
            lazy_file->re_file = file_reopen(file_page->re_file);
            lazy_file->page_read_bytes = file_page->page_read_bytes;
            lazy_file->page_zero_bytes = file_page->page_zero_bytes;
            lazy_file->page_cnt = calloc(1, sizeof(size_t));
            lazy_file->page_cnt = file_page->page_cnt;
            if(!vm_alloc_page_with_initializer(page_get_type(src_p), src_p->va, src_p->writable, lazy_load_file, lazy_file)) {
                goto err;
            }
            struct page *dst_p = spt_find_page(dst, src_p->va);
            if (dst_p == NULL)
                goto err;
            if (!vm_do_claim_page(dst_p)) 
                goto err;
            memcpy(dst_p->frame->kva, src_p->frame->kva, PGSIZE);
            break;
        }
        case VM_UNINIT: ;
            struct lazy_aux *lazy_aux = calloc(1, sizeof(struct lazy_aux));
            memcpy(lazy_aux, src_p->uninit.aux, sizeof(struct lazy_aux));
            if(!vm_alloc_page_with_initializer(page_get_type(src_p), src_p->va, src_p->writable, src_p->uninit.init, lazy_aux)) {
                goto err;
            }
            break;
        }
    }
    return true;
err:
    return false;
}
