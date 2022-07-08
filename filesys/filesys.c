#include "filesys/filesys.h"

#include <debug.h>
#include <stdio.h>
#include <string.h>

#include "devices/disk.h"
#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"

#include "filesys/fat.h" /**/
#include "threads/thread.h" /**/

/* The disk that contains the file system. */
struct disk *filesys_disk;
struct list sym_list; /* symbolic link : 파일(디렉토리)에 직접 접근*/

static void do_format(void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void 
filesys_init(bool format) { // init.c -> main에서 call (disk init -> filesys init)
    // 처음에는 format으로 call됨
    filesys_disk = disk_get(0, 1); // get disk partition (partition?)
    if (filesys_disk == NULL)
        PANIC("hd0:1 (hdb) not present, file system initialization failed");

    inode_init(); // list_init(&open_inode)

#ifdef EFILESYS : 살려!!!
    /* fat initializing : allocate struct fat_fs 
     * copy boot sector to fat_fs->bs
     * fat check (fat_boot->magic) and initialize file-allocate-table
     * call fat_fs_init : set fat lenth and point start sector */
    fat_init(); 

    if (format)
        do_format(); // 빈깡통 FAT...
    fat_open(); // fat sector의 fat 불러와서 열기
    list_init(&sym_list); // symbolic link list init

#else : 살려!!!

    /* Original FS */
    free_map_init();

    if (format)
        do_format();

    free_map_open();

#endif : 살려!!!
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void 
filesys_done(void) {
    /* Original FS */
#ifdef EFILESYS
    fat_close();
#else
    free_map_close();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
/**/
bool 
filesys_create(const char *name, off_t initial_size) {
    disk_sector_t inode_sector = 0;
    // thread에 current directory가 추가되어 있음
    // 접근한 최종 directory 를 기록함
    struct thread *curr = thread_current();
    // current directory가 없는 경우 root directory로 접근
    struct dir *dir = curr->current_dir == NULL ? dir_open_root() : curr->current_dir;
    // fat allocate로 size만큼 chain create하고, inode 만들고, directoty에 파일 추가
    bool success = (dir != NULL && fat_allocate(1, &inode_sector) && inode_create(inode_sector, initial_size) && dir_add(dir, name, inode_sector));
    if (!success && inode_sector != 0)
        fat_remove_chain(inode_sector, 0); // 실패한 경우 확보한 fat entry 초기화
    inode_set_file(inode_open(inode_sector)); // 파일 오픈, inode 세팅

    return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
/**/
struct file *
filesys_open(const char *name) {
    struct thread *curr = thread_current(); // directory 확인용
    struct dir *dir = curr->current_dir == NULL ? dir_open_root() : curr->current_dir;
    struct inode *inode = NULL;

    if (dir != NULL) // directory에서 찾아서 열어준다
        dir_lookup(dir, name, &inode);

    return file_open(inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
/**/
bool filesys_remove(const char *name) {
    struct thread *curr = thread_current(); // directory 확인용
    struct dir *dir = curr->current_dir == NULL ? dir_open_root() : curr->current_dir;
    bool success = dir != NULL && dir_remove(dir, name);

    return success;
}

/* Formats the file system. */
static void
do_format(void) {
    printf("Formatting file system...");

#ifdef EFILESYS
    /* Create FAT and save it to the disk. */
    fat_create(); // 새로운 fat를 만들고
    fat_close();  // 바로 닫아서 빈 채로 저장해버리기
#else
    free_map_create();
    if (!dir_create(ROOT_DIR_SECTOR, 16))
        PANIC("root directory creation failed");
    free_map_close();
#endif

    printf("done.\n");
}