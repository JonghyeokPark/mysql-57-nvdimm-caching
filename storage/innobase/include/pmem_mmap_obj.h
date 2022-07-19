#ifndef __PMEMMMAPOBJ_H_
#define __PMEMMMAPOBJ_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>

#include <pthread.h>
#include <map>
#include <vector>

//#include "ut0new.h"
//#include "log0log.h"

// (jhpark): this header file for UNIV_NVDIMM_CACHE
//					 use persistent memroy with mmap on dax-enabled file system
//					 numoerous data structures and functions are silimilar to 
//					 ones in pmem_obj.h

#define NVDIMM_MMAP_FILE_NAME         "nvdimm_mmap_file"
#define PMEM_MMAP_MAX_FILE_NAME_LENGTH    10000

#define PMEMMMAP_INFO_PRINT(fmt, args...)              \
	fprintf(stderr, "[PMEMMMAP_INFO]: %s:%d:%s():" fmt,  \
	__FILE__, __LINE__, __func__, ##args)             	 \

#define PMEMMMAP_ERROR_PRINT(fmt, args...)             \
	fprintf(stderr, "[PMEMMMAP_ERROR]: %s:%d:%s():" fmt, \
	__FILE__, __LINE__, __func__, ##args)             	 \

// pmem_persistent 
#define CACHE_LINE_SIZE 64
#define ASMFLUSH(dst) __asm__ __volatile__ ("clflush %0" : : "m"(*(volatile char *)dst))

static inline void clflush(volatile char* __p) {   
	asm volatile("clflush %0" : "+m" (*__p));
}

static inline void mfence() {   
	asm volatile("mfence":::"memory");
  return;
}

static inline void flush_cache(void *ptr, size_t size){
  unsigned int  i=0;
  uint64_t addr = (uint64_t)ptr;
    
  mfence();
  for (i =0; i < size; i=i+CACHE_LINE_SIZE) {
    clflush((volatile char*)addr);
    addr += CACHE_LINE_SIZE;
  }
  mfence();
}
static inline void memcpy_persist
                    (void *dest, void *src, size_t size){
  unsigned int  i=0;
  uint64_t addr = (uint64_t)dest;

  memcpy(dest, src, size);

  mfence();
  for (i =0; i < size; i=i+CACHE_LINE_SIZE) {
    clflush((volatile char*)addr);
    addr += CACHE_LINE_SIZE;
  }
  mfence();
}


// buffer
struct __pmem_mmap_buf_sys;
typedef struct __pmem_mmap_buf_sys PMEM_MMAP_BUF_SYS;

//#define PMEM_MMAP_MTRLOG_HDR_SIZE sizeof(PMEM_MMAP_MTRLOG_HDR)
//#define PMEM_MMAP_LOGFILE_HEADER_SZ sizeof(PMEM_MMAP_MTRLOGFILE_HDR)

#define PMEM_MMAP_MTR_FIL_HDR_SIZE_OFFSET 0
#define PMEM_MMAP_MTR_FIL_HDR_LSN 8
#define PMEM_MMAP_MTR_FIL_HDR_CKPT_LSN 16
#define PMEM_MMAP_MTR_FIL_HDR_CKPT_OFFSET 24
#define PMEM_MMAP_MTR_FIL_HDR_SIZE 32

/////////////////////////////////////////////////////////

//extern PMEM_MMAP_MTRLOG_BUF* mmap_mtrlogbuf;

// buffer
extern PMEM_MMAP_BUF_SYS* mmap_buf_sys;

// wrapper function (see pemm_obj.h)
// mmap persistent memroy region on dax file system
unsigned char* pm_mmap_create(const char* path, const uint64_t pool_size);
// unmmap persistent memory 
void pm_mmap_free(const uint64_t pool_size);


/* mtr log */
// data structure in pmem_obj
struct __pmem_mmap_mtrlog_buf {
  pthread_mutex_t mtrMutex; // mutex protecting writing to mtr log region
  bool need_recv;       // recovery flag
  
//  ib_uint64_t  mtr_sys_lsn;   // global lsn for mtr_lsn (monotically increased) (stale)
//  ib_uint64_t last_ckpt_lsn;  // checkpoint_lsn (oldest page LSN in NVDIMM caching
                        // flsuher list (stale)
	
	size_t ckpt_offset; 	// we can remove mtr log up to this offset 

	size_t size;          // total size of mtr log region
	size_t cur_offset;    // current offset of mtr log region
	size_t prev_offset; 	// prev_offset

};

struct __pmem_mmap_buf_sys {
	pthread_mutex_t bufMutex;	// mutex protecting writing NC-page to NVDIMM
	size_t size;							// total size of NVDIMM caching page region
	unsigned long n_pages;						// total number of pages on NC buffer region 
	unsigned long cur_offset;					// current offset
};

// buffer
void pm_mmap_buf_init(const uint64_t size);
void pm_mmap_buf_free(void);
void pm_mmap_buf_write(unsigned long len, void* buf);

bool pm_mmap_recv(uint64_t start_offset, uint64_t end_offset);

// TODO(jhpark): covert these variables as structure (i.e., recv_sys_t)
extern bool is_pmem_recv;
extern uint64_t pmem_recv_offset;
extern uint64_t pmem_recv_size;

extern std::map<std::pair<uint64_t,uint64_t> , std::vector<uint64_t> > pmem_nc_buffer_map;
extern std::map<std::pair<uint64_t,uint64_t> , std::vector<uint64_t> > pmem_nc_page_map;
uint64_t pm_mmap_recv_check_nc_buf(uint64_t space, uint64_t page_no);
void nc_recv_analysis();

extern std::map<std::pair<uint64_t,uint64_t> , uint64_t > pmem_nc_shadow_map;

// YYY
extern unsigned char *gb_pm_mtrlog;
#define MTR_LOG_SIZE_PER_PAGE 1024
#define MTR_LOG_PAGE_NUM  (1024*1024*1024UL/4096)

struct __mtr_log_insert_hdr {
  int type;          // INSERT
  uint64_t space;
  uint64_t page_no;
  uint64_t rec_offset; // page offset (121 ~ 4086)
  uint64_t rec_size;   // record size
  uint64_t cur_rec_off; // 2B rec
  uint64_t pd_offset;  // page offset
  uint64_t pd_size;    // page directory size
  int valid;           // valid
};

struct __mtr_log_update_hdr {
  int type;
  uint64_t space;
  uint64_t page_no;
  uint64_t rec_off;
  uint64_t rec_size;
  int valid;
};

struct __mtr_log_delete_hdr {
  int type;
  uint64_t space;
  uint64_t page_no;
  uint64_t rec_off;
  uint64_t rec_size;
  int valid;
};

typedef struct __mtr_log_insert_hdr PMEM_MTR_INSERT_LOGHDR;
typedef struct __mtr_log_update_hdr PMEM_MTR_UPDATE_LOGHDR;
typedef struct __mtr_log_delete_hdr PMEM_MTR_DELETE_LOGHDR;

extern std::map<uint64_t, uint64_t> pmem_mtrlog_offset_map; // (frame_id, offset)

void nc_mtrlog_analysis();
void nc_recv_split_shadow();
int nc_mtrlog_recv_read_hdr(unsigned char* addr);

#endif  /* __PMEMMAPOBJ_H__ */
