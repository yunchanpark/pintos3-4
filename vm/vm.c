/* vm.c: Generic interface for virtual memory objects. */

#include "threads/malloc.h"
#include "vm/vm.h"
#include "vm/inspect.h"


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
    list_init(&frame_list);
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
        
        uninit_new(page, upage, init, type, aux, initializer); /*** debugging ddalgui : type? marker? ***/
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
	if(hash_insert(spt->spt_hash, &page->spt_elem) == NULL) {
        succ = true;
    }
	return succ;
}

void
spt_remove_page (struct supplemental_page_table *spt, struct page *page) {
	vm_dealloc_page (page);
	return true;
}

/* Get the struct frame, that will be evicted. */
static struct frame *
vm_get_victim (void) {
	struct frame *victim = NULL;
	 /* TODO: The policy for eviction is up to you. */

	return victim;
}

/* Evict one page and return the corresponding frame.
 * Return NULL on error.*/
static struct frame *
vm_evict_frame (void) {
	struct frame *victim UNUSED = vm_get_victim ();
	/* TODO: swap out the victim and return the evicted frame. */

	return NULL;
}

/* palloc() and get frame. If there is no available page, evict the page
 * and return it. This always return valid address. That is, if the user pool
 * memory is full, this function evicts the frame to get the available memory
 * space.*/
/*** team 7 ***/
static struct frame *
vm_get_frame (void) {
	struct frame *frame = calloc(1, sizeof(struct frame));
	ASSERT (frame != NULL);

    void *temp = palloc_get_page(PAL_USER);
    
    if(!temp) {
        PANIC("todo"); // implement later
    }

    frame->kva = temp;
    list_push_front(&frame_list, &frame->f_elem);

	ASSERT (frame->page == NULL);
	return frame;
}

/* Growing the stack. */
static void
vm_stack_growth (void *addr UNUSED) {
}

/* Handle the fault on write_protected page */
static bool
vm_handle_wp (struct page *page UNUSED) {
}

/* Return true on success */
bool
vm_try_handle_fault (struct intr_frame *f UNUSED, void *addr UNUSED,
		bool user UNUSED, bool write UNUSED, bool not_present UNUSED) {
	struct supplemental_page_table *spt UNUSED = &thread_current ()->spt;
	struct page *page = NULL;
	/* TODO: Validate the fault */
	/* TODO: Your code goes here */
    page = spt_find_page(spt, addr); /* 변경 */
    if (page == NULL) {
        goto err;
    }
    else {
        return vm_do_claim_page (page);
    }

    // if (page->operations->type == VM_UNINIT)
    //     return vm_do_claim_page (page);
    // else 
    //     goto err;

err :
    return false;
}

/* Free the page.
 * DO NOT MODIFY THIS FUNCTION. */
void
vm_dealloc_page (struct page *page) {
	destroy (page);
	free (page);
}

/* Claim the page that allocate on VA. */
/*** team 7 ***/
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
	struct frame *frame = vm_get_frame ();

	/* Set links */
	frame->page = page;
	page->frame = frame;

	/* TODO: Insert page table entry to map page's VA to frame's PA. */
    /* from install_page */
    struct thread *t = thread_current ();

	if (pml4_get_page (t->pml4, page->va) == NULL // pml4에 upage unmapping 상태이면
			&& pml4_set_page (t->pml4, page->va, frame->kva, page->writable)) // pml4 set 함
        return swap_in (page, frame->kva);

    return false;
}

/* Initialize new supplemental page table */
/*** team 7 ***/
void
supplemental_page_table_init (struct supplemental_page_table *spt UNUSED) {
    spt->spt_hash = (struct hash *)calloc(1, sizeof(struct hash));
    hash_init(spt->spt_hash, page_hash, page_less, NULL);
}

/* Copy supplemental page table from src to dst */
/* team 7 : hyeRexx */
bool
supplemental_page_table_copy (struct supplemental_page_table *dst UNUSED,
		struct supplemental_page_table *src UNUSED) {
    struct hash_iterator i;

    hash_first (&i, src->spt_hash);
    while (hash_next (&i)) // iterate src
    {      
        // 1. iterate src spt and get ref page
        struct page *src_p = hash_entry (hash_cur (&i), struct page, spt_elem);

        // 2. allocate new page at dst(current thread)
        //    이 단계에서 struct page 구성 자체는 완료됨 (vm_alloc + uninit_new)
        bool check = vm_alloc_page_with_initializer(src_p->uninit.type, src_p->va, src_p->writable, src_p->uninit.page_initializer, src_p->uninit.aux);
        // bool check = vm_alloc_page(src_p->uninit.type, src_p->va, src_p->writable);
        // ASSERT (check != false);
        if (!check) 
            goto err;
            
        // 3. get new page : find page from dst spt (vm alloc returns bool type)
        //    다음 작업을 위해서 새로 만든 페이지를 꺼내어 놓음
        struct page *dst_p = spt_find_page(dst, src_p->va);
        // ASSERT (dst_p != NULL);
        if (!dst_p)
            goto err;

        // 4. virtual - physical mapping (dst)
        check = vm_do_claim_page(dst_p);
        if (!check) 
            goto err;

        // 5. get src page's content
        switch(VM_TYPE(src_p->operations->type)) {
            case VM_ANON :
                memcpy(dst_p->frame->kva, src_p->frame->kva, PGSIZE);
                break;
            
            case VM_FILE :
                memcpy(dst_p->frame->kva, src_p->frame->kva, PGSIZE);
                break;
            
            /**/
            // case VM_UNINIT :
            //     memcpy(dst_p->uninit.aux, src_p->uninit.aux, sizeof(src_p->uninit.aux));
            //     break;
        }
    }
    return true;

err :
    return false;
}

/* Free the resource hold by the supplemental page table */
void
supplemental_page_table_kill (struct supplemental_page_table *spt UNUSED) {
	/* TODO: Destroy all the supplemental_page_table hold by thread and
	 * TODO: writeback all the modified contents to the storage. */
    struct hash_iterator i;

    hash_first (&i, spt->spt_hash);
    while (hash_next (&i))
    {
        struct page *curr_p = hash_entry (hash_cur (&i), struct page, spt_elem);
        destroy(curr_p);
    }
}
