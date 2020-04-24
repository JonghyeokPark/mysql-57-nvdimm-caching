#ifndef __PMEMMMAPOBJ_H_
#define __PMEMMMAPOBJ_H_

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <unistd.h>

#include "log0log.h"

// (jhpark): this header file for UNIV_NVDIMM_CACHE
//					 use persistent memroy with mmap on dax-enabled file system
//					 numoerous data structures and functions are silimilar to 
//					 ones in pmem_obj.h

// TODO(jhpark) : separate cmopile option (-DUNIV_PMEM_MMAP)
// TODO(jhpark) : redesign for configurable NVDIMM caching options

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

struct __pmem_mmap_mtrlog_buf;
typedef struct __pmem_mmap_mtrlog_buf PMEM_MMAP_MTRLOG_BUF;

struct __pmem_mmap_mtrlog_hdr;
typedef struct __pmem_mmap_mtrlog_hdr PMEM_MMAP_MTRLOG_HDR;

struct __pmem_mmap_mtrlog_fileheader;
typedef struct __pmem_mmap_mtrlog_fileheader PMEM_MMAP_MTRLOGFILE_HDR;

#define PMEM_MMAP_MTRLOG_HDR_SIZE sizeof(PMEM_MMAP_MTRLOG_HDR)
#define PMEM_MMAP_LOGFILE_HEADER_SZ sizeof(PMEM_MMAP_MTRLOGFILE_HDR)

/* PMEM_MMAP mtrlog file header */

// TOOD(jhpark) remove unncessary part after fixing recovery algorithm
// (jhpark): more offset will be added if recovery algorithm fixed.
// recovery flag
//const uint32_t PMEM_MMAP_RECOVERY_FLAG = 0;
// mtrlog checkpoint lsn; recovery start from this lsn (i.e., offset)
//const uint32_t PMEM_MMAP_LOGFILE_CKPT_LSN = 1;
// PMEM_MMAP mtrlog file header size
//const uint32_t PMEM_MMAP_LOGFILE_HEADER_SZ = PMEM_MMAP_RECOVERY_FLAG
//                                             + PMEM_MMAP_LOGFILE_CKPT_LSN;

//const uint32_t PMEM_MMAP_LOGFILE_HEADER_SZ = sizeof(__pmem_mmap_mtrlog_buf);
//const uint32_t PMEM_MTRLOG_BLOCK_SZ = 256

#define PMEM_MMAP_MTR_FIL_HDR_LSN 0
#define PMEM_MMAP_MTR_FIL_HDR_CKPT_LSN 8
#define PMEM_MMAP_MTR_FIL_HDR_SIZE 16

/////////////////////////////////////////////////////////

// wrapper function (see pemm_obj.h)
// mmap persistent memroy region on dax file system
char* pm_mmap_create(const char* path, const uint64_t pool_size);
// unmmap persistent memory 
void pm_mmap_free(const uint64_t pool_size);


/* mtr log */
// data structure in pmem_obj
struct __pmem_mmap_mtrlog_buf {
  LogSysMutex mutex;    // mutex protecting writing to mtr log region
  bool need_recv;       // recovery flag
  
  lsn_t  mtr_sys_lsn;   // global lsn for mtr_lsn (monotically increased)
  lsn_t last_ckpt_lsn;  // checkpoint_lsn (oldest page LSN in NVDIMM caching
                        // flsuher list
	size_t size;          // total size of mtr log region
	size_t cur_offset;    // current offset of mtr log region

};

// mtr log file header
struct __pmem_mmap_mtrlog_fileheader {
  lsn_t mtr_sys_lsn;
  lsn_t max_ckpt_lsn;
  uint64_t size; 
  //size_t cur_offset;
};

struct __pmem_mmap_mtrlog_hdr {
	unsigned long int len;    // length of mtr log payload
	unsigned long int lsn;      // lsn from global log_sys
  unsigned long int mtr_lsn;  // mtr log lsn
};

// logging? 

int pm_mmap_mtrlogbuf_init(const size_t size);
void pm_mmap_mtrlogbuf_mem_free();
void pm_mmap_write_logfile_header_size(size_t size);
ssize_t pm_mmap_mtrlogbuf_write(const uint8_t* buf, 
                                unsigned long int n, unsigned long int lsn);

#endif  /* __PMEMMAPOBJ_H__ */
