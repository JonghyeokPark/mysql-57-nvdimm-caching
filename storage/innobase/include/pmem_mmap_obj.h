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
//#include "ut0new.h"
//#include "log0log.h"

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

// jhpark: for validate NC page
struct __pmem_mmap_mtrlog_placeholder;
typedef struct __pmem_mmap_mtrlog_placeholder PMEM_MMAP_PLACEHOLDER;
#define PMEM_MMAP_PLACEHODER_SZ sizeof(PMEM_MMAP_PLACEHOLDER)

// buffer
struct __pmem_mmap_buf_sys;
typedef struct __pmem_mmap_buf_sys PMEM_MMAP_BUF_SYS;

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

#define PMEM_MMAP_MTR_FIL_HDR_SIZE_OFFSET 0
#define PMEM_MMAP_MTR_FIL_HDR_LSN 8
#define PMEM_MMAP_MTR_FIL_HDR_CKPT_LSN 16
#define PMEM_MMAP_MTR_FIL_HDR_CKPT_OFFSET 24
#define PMEM_MMAP_MTR_FIL_HDR_SIZE 32

/////////////////////////////////////////////////////////

extern PMEM_MMAP_MTRLOG_BUF* mmap_mtrlogbuf;


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
  unsigned int need_recv;       // recovery flag
	size_t ckpt_offset; 	// we can remove mtr log up to this offset 
  size_t min_invalid_offset; // minimum ckpt_offset 
  size_t max_invalid_offset; // maximum ckpt_offset

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

// mtr log file header
struct __pmem_mmap_mtrlog_fileheader {
  size_t size;
	size_t flushed_lsn;
	size_t ckpt_lsn;
	size_t ckpt_offset; 
};

struct __pmem_mmap_mtrlog_hdr {
	unsigned int need_recv;								 // true if need recovery
	unsigned long int len;    		 // length of mtr log payload
	unsigned long int lsn;      	 // lsn from global log_sys
  unsigned long int mtr_lsn;  	 // mtr log lsn
	unsigned long int prev_offset; // prev mtr log header offset
	
	unsigned long int space;			 // undo page space id
	unsigned long int page_no;		 // undo page page_no
};

// for validation
struct __pmem_mmap_mtrlog_placeholder {
  uint64_t is_valid;
  unsigned long space_id;
  unsigned long page_no;
  uint64_t  trx_id;
};

// logging? 
int pm_mmap_mtrlogbuf_init(const size_t size);
void pm_mmap_mtrlogbuf_mem_free();
void pm_mmap_read_logfile_header(PMEM_MMAP_MTRLOGFILE_HDR* mtrlog_fil_hdr);
void pm_mmap_write_logfile_header_size(size_t size);
void pm_mmap_write_logfile_header_lsn(uint64_t lsn);
void pm_mmap_write_logfile_header_ckpt_info(uint64_t offset, uint64_t lsn);
uint64_t pm_mmap_log_checkpoint(uint64_t cur_offset);
void pm_mmap_log_commit(unsigned long cur_space, unsigned long cur_page, uint64_t cur_offset);

ssize_t pm_mmap_mtrlogbuf_write(const uint8_t* buf, 
                                unsigned long int n, unsigned long int lsn);

bool pm_mmap_mtrlogbuf_identify(size_t offset, unsigned long space, unsigned long page_no);
void pm_mmap_mtrlogbuf_unset_recv_flag(size_t offset);

// jhpark-validation
void pm_mmap_mtrlogbuf_record(unsigned long space, unsigned long page_no, uint64_t trx_id);
void pm_mmap_mtrlogbuf_log_commit(unsigned long space, unsigned long page_no, uint64_t trx_id);
void pm_mmap_mtrlogbuf_check();
bool pm_mmap_mtrlogbuf_validate(unsigned long space_id, unsigned long page_no);

void pm_mmap_mtrlogbuf_commit(unsigned char* rec, unsigned long cur_rec_size, unsigned long space, unsigned long page_no);
void pm_mmap_mtrlogbuf_commit_v1(unsigned long space, unsigned long page_no);

// jhpark-recovery
 int pm_mmap_memcmp(const unsigned char *tmp1, const unsigned char *tmp2, unsigned long len);

// buffer
void pm_mmap_buf_init(const uint64_t size);
void pm_mmap_buf_free(void);
void pm_mmap_buf_write(unsigned long len, void* buf);
//unsigned char* pm_mmap_buf_chunk_alloc(unsigned long mem_size, ut_new_pfx_t* pfx);


// recovery
//bool pm_mmap_recv(PMEM_MMAP_MTRLOGFILE_HDR* log_fil_hdr);
bool pm_mmap_recv(uint64_t start_offset, uint64_t end_offset);
uint64_t pm_mmap_recv_check(PMEM_MMAP_MTRLOGFILE_HDR* log_fil_hdr);
void pm_mmap_recv_flush_buffer();

// TODO(jhpark): covert these variables as structure (i.e., recv_sys_t)
extern bool is_pmem_recv;
extern uint64_t pmem_recv_offset;
extern uint64_t pmem_recv_size;

/** Recovery system data structure */
bool pm_mmap_recv_nc_page_validate(unsigned long space_id, unsigned long page_no);
void pm_mmap_recv_add_active_trx_list(unsigned long trx_id);
void pm_mmap_recv_show_trx_list();
bool pm_mmap_recv_nc_page_copy(unsigned long space_id, unsigned long page_no, void* buf);
void pm_for_debug_REDO(uint64_t size);

//=======================================================================================================

/* Hello */
struct __pmem_log_buf;
typedef struct __pmem_log_buf PMEM_LOG_BUF;
#define PMEM_LOG_BUF_SZ sizeof(PMEM_LOG_BUF)

struct __pmem_log_hdr;
typedef struct __pmem_log_hdr PMEM_LOG_HDR;
#define PMEM_LOG_HDR_SZ sizeof(PMEM_LOG_HDR)

extern PMEM_LOG_BUF* mmap_logbuf;

struct __pmem_log_buf {
  pthread_mutex_t logMutex; 
  size_t        size;              // log size
  unsigned char*         data;     // actual log data
  uint64_t      lsn;               // log sequence number
  unsigned int          need_recv;         // for recovery
  uint64_t      cur_offset;        // current offset
};

struct __pmem_log_hdr {
	unsigned int need_recv;				 // 0: none 1: need recovery 2: flush (don't need to apply)
	unsigned long int len;    		 // length of mtr log payload
	unsigned long int lsn;      	 // lsn from global log_sys
  // still needed?
	unsigned long int space;			 // undo page space id
	unsigned long int page_no;		 // undo page page_no
};


// allocate log buffer on NVM
void pmem_log_init(const size_t size);
ssize_t pmem_log_write(unsigned char *buf, unsigned long int len, unsigned long int lsn,
                    unsigned long int space, unsigned long int page_no);
bool pmem_log_validate(unsigned char *check);

// flush log
void pmem_log_flush(unsigned long int space, unsigned long int page_no);

// checkpoint 
bool pmem_log_checkpoint();

#endif  /* __PMEMMAPOBJ_H__ */
