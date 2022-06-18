#ifndef DEVICES_DISK_H
#define DEVICES_DISK_H

#include <inttypes.h>
#include <stdint.h>

/* Size of a disk sector in bytes. */
#define DISK_SECTOR_SIZE 512 
// 디스크의 1 섹터 = 512 바이트
// bytes_to_sectors (off_t size) 사용하면, PGSIZE에 몇 개의 섹터가 필요한지 구할 수 있음.

/* Index of a disk sector within a disk.
 * Good enough for disks up to 2 TB. */
typedef uint32_t disk_sector_t; // 하나의 인덱스가 하나의 디스크 섹터에 해당함.

/* Format specifier for printf(), e.g.:
 * printf ("sector=%"PRDSNu"\n", sector); */
#define PRDSNu PRIu32 // 섹터를 printf 할 때, 사용가능한 포맷

void disk_init (void); //디스크 서브시스템 자체를 initialize 해주고, 디스크를 감지해주는 함수.
void disk_print_stats (void); //디스크 별로 read, write가 각각 몇회차씩 이루어졌는지 확인하는 함수. 

struct disk *disk_get (int chan_no, int dev_no); // PintOS에서는 disk_get(1,1)을 swap으로 사용하고 있음
disk_sector_t disk_size (struct disk *); // 디스크의 사이즈를 섹터의 개수를 단위로 돌려주는 함수.
void disk_read (struct disk *, disk_sector_t, void *); // 디스크의 idx번째 섹터를 읽어서 버퍼로 저장해주는 함수
void disk_write (struct disk *, disk_sector_t, const void *); // 디스크의 idx번째 섹터에다가 버퍼로부터 쓰기해주는 함수.

void 	register_disk_inspect_intr ();
#endif /* devices/disk.h */
