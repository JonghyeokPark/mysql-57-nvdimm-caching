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

#include <fstream>
#include <cassert>
#include <iostream>
#include <iterator>
#include <algorithm>


#define NC_LOG_SIZE (2*1024*1024*1024UL)
// TODO(jhpark): make it configurable!!!
extern int nc_log_fd;
extern unsigned char* nc_log_ptr;
extern uint64_t nc_log_offset;
const char filename[] = "/tmp/nclog.log";

extern uint64_t min_nc_page_lsn;
extern uint64_t org_page_lsn;

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
	size_t ckpt_offset; 	// we can remove mtr log up to this offset 
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

void nc_set_in_update_flag(unsigned char* frame);
void nc_unset_in_update_flag(unsigned char* frame);
extern uint64_t in_update_page;


// redo log
struct nc_redo{
  uint64_t nc_buf_free = 0;
  uint64_t nc_lsn = 0;
};

// redo info spot
#define REDO_INFO_OFFSET  (512*1024*1024)
extern nc_redo* nc_redo_info;

// redo log for NC pages threshold
#define NC_REDO_LOG_THRESHOLD 100000
extern uint64_t latest_nc_oldest_lsn;
extern bool nc_flush_flag;

#endif  /* __PMEMMAPOBJ_H__ */
