#include "pmem_mmap_obj.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "ut0mutex.h"

// gloabl persistent memmory region
char* gb_pm_mmap;
int gb_pm_mmap_fd;
PMEM_MMAP_MTRLOG_BUF* mmap_mtrlogbuf = NULL;

char* pm_mmap_create(const char* path, const uint64_t pool_size) {
  
  if (access(path, F_OK) != 0) {
    gb_pm_mmap_fd = open(path, O_RDWR | O_CREAT, 0777); 
    if (gb_pm_mmap_fd < 0) {
      PMEMMMAP_ERROR_PRINT("pm_mmap_file open failed\n");
      return NULL;
    }

    // set file size as pool_size
    if (truncate(path, pool_size) == -1) {
      PMEMMMAP_ERROR_PRINT("pm_mmap_file turncate failed\n");
    }

    gb_pm_mmap = (char *) mmap(NULL, pool_size, PROT_READ|PROT_WRITE, MAP_SHARED, gb_pm_mmap_fd, 0);
    if (gb_pm_mmap == MAP_FAILED) {
      PMEMMMAP_ERROR_PRINT("pm_mmap mmap() failed\n");
    }
    memset(gb_pm_mmap, 0x00, pool_size);

  } else {
    // TODO(jhaprk) add the recovery logic
    PMEMMMAP_INFO_PRINT("RECOVERY START!!!\n");
    gb_pm_mmap_fd = open(path, O_RDWR, 0777);

    if (gb_pm_mmap_fd < 0) {
      PMEMMMAP_ERROR_PRINT("pm_mmap_file open failed\n");
      return NULL;
    }

    gb_pm_mmap = (char *) mmap(NULL, pool_size, PROT_READ|PROT_WRITE, MAP_SHARED, gb_pm_mmap_fd, 0);
    if (gb_pm_mmap == MAP_FAILED) {
      PMEMMMAP_ERROR_PRINT("pm_mmap mmap() faild recovery failed\n");
    }

    PMEM_MMAP_MTRLOG_BUF* recv_mmap_mtrlog_buf = (PMEM_MMAP_MTRLOG_BUF*) malloc(sizeof(PMEM_MMAP_MTRLOG_BUF));
    PMEM_MMAP_MTRLOG_HDR* recv_mmap_mtrlog_hdr = (PMEM_MMAP_MTRLOG_HDR*) malloc(PMEM_MMAP_MTRLOG_HDR_SIZE);
    size_t recv_size = recv_mmap_mtrlog_buf->cur_offset;

    // Get header information from exsiting nvdimm log file
    memcpy(recv_mmap_mtrlog_hdr, gb_pm_mmap+recv_size, PMEM_MMAP_MTRLOG_HDR_SIZE);
    //size_t recv_prev_offset = recv_mmap_mtrlog_hdr->prev;
    memset(recv_mmap_mtrlog_hdr, 0x00, PMEM_MMAP_MTRLOG_HDR_SIZE);
    //memcpy(recv_mmap_mtrlog_hdr, gb_pm_mmap+recv_prev_offset, PMEM_MMAP_MTRLOG_HDR_SIZE);

    // debug
    //fprintf(stderr, "size: %lu\n", recv_size);
    //fprintf(stderr, "len: %lu\n", recv_mmap_mtrlog_hdr->len);
    //fprintf(stderr, "lsn: %lu\n", recv_mmap_mtrlog_hdr->lsn);
    //fprintf(stderr, "need_recovery: %d\n", recv_mmap_mtrlog_hdr->need_recv);

    free(recv_mmap_mtrlog_buf);
    free(recv_mmap_mtrlog_hdr);  
  }

  // Force to set NVIMMM
  setenv("PMEM_IS_PMEM_FORCE", "1", 1);
  PMEMMMAP_INFO_PRINT("Current kernel does not recognize NVDIMM as the persistenct memory \
      We force to set the environment variable PMEM_IS_PMEM_FORCE \
      We call mync() instead of mfense()\n");

  return gb_pm_mmap;
}

void pm_mmap_mtrlogbuf_mem_free() {
  if (mmap_mtrlogbuf != NULL) {
    // TODO(jhpark): free mtr recovery system
    ut_free(mmap_mtrlogbuf);
    mmap_mtrlogbuf = NULL;
  }
}

void pm_mmap_free(const uint64_t pool_size) {
 munmap(gb_pm_mmap, pool_size);
 close(gb_pm_mmap_fd);
 // free mtr log system
 pm_mmap_mtrlogbuf_mem_free();
 PMEMMMAP_INFO_PRINT("munmap persistent memroy region\n");
}


// allocate mtr log buffer
int pm_mmap_mtrlogbuf_init(const size_t size) {

  mmap_mtrlogbuf = static_cast<PMEM_MMAP_MTRLOG_BUF*>(ut_zalloc_nokey(sizeof(PMEM_MMAP_MTRLOG_BUF)));
  //mutex_create(LATCH_ID_NVDIMM_MTRLOG_MUTEX, &mmap_mtrlogbuf->mutex);

  mmap_mtrlogbuf->size = size;
  mmap_mtrlogbuf->cur_offset = 0;
  PMEMMMAP_INFO_PRINT("mtr log buffer initialization finished!\n"); 
  return 0;
}

// write header offset (i.e., size) of latest mtr log
void pm_mmap_write_logfile_header_size(uint64_t size) {
  byte hdr_size[8];
  mach_write_to_8(hdr_size, size);
  fprintf(stderr, "[JONGQ] pm_mmap_write_logfile_header_size: %lu %lu\n", size, hdr_size);
  memcpy(gb_pm_mmap, hdr_size, 8);
}

// write oldeset_lsn of NVDIMM-caching page
// TODO(jhpark): is this right?
void pm_mmap_write_logfile_header_lsn(lsn_t lsn) {
	byte hdr_lsn[8];
	mach_write_to_8(hdr_lsn, lsn);
	fprintf(stderr, "[JONGQ] pm_mmap_write_logfile_header_lsn when flush: %lu %lu\n", lsn, hdr_lsn);
	memcpy(gb_pm_mmap, hdr_lsn, 8);
}

// write mtr log
ssize_t pm_mmap_mtrlogbuf_write( 
    const uint8_t* buf,
    unsigned long int n,
    unsigned long int lsn) 
{
  unsigned long int ret = 0;

  // TODO(jhpark) : mutex add
  //PMEM_MMAP_MTRLOG_BUF* mmap_mtrlogbuf = (PMEM_MMAP_MTRLOG_BUF*) malloc(sizeof(PMEM_MMAP_MTRLOG_BUF));
  //memcpy(mmap_mtrlogbuf, gb_pm_mmap, sizeof(PMEM_MMAP_MTRLOG_BUF));
  
  char* pdata = (char *) gb_pm_mmap + PMEM_MMAP_LOGFILE_HEADER_SZ;
  // (jhpark): original version of mtr logging relies on external offset value
  //           which is persisted on every updates
  //           cur_offset points offset which header will be appended
  if (mmap_mtrlogbuf == NULL) {
    PMEMMMAP_ERROR_PRINT("mmap_mtrlogbuf is NULL!\n");
		return -1;
  }

  size_t offset = mmap_mtrlogbuf->cur_offset;
  fprintf(stderr, "[JONGQ] current offset: %lu\n", offset);

  // Fill the mmap_mtr log header info.
  PMEM_MMAP_MTRLOG_HDR* mmap_mtr_hdr = (PMEM_MMAP_MTRLOG_HDR*) malloc(PMEM_MMAP_MTRLOG_HDR_SIZE);
  mmap_mtr_hdr->len = n;
  // lsn from log_sys
  mmap_mtr_hdr->lsn = lsn;
  // lsn from mtr_log_sys
  mmap_mtr_hdr->mtr_lsn = mmap_mtrlogbuf->mtr_sys_lsn+1;

  // jhpark: no checkpoint + recovery version;
  //         just write into mtr log
  if (offset >= 1024*1024*1024*3UL) {
    offset = 0;
  }

	// TODO(jhaprk) : checkpoint
  // Check free space in mmap_mtr log area
  //PMEM_MMAP_MTRLOG_HDR* cur_mmap_mtr_hdr = (PMEM_MMAP_MTRLOG_HDR*)(pdata + offset);
  //size_t needed_space = PMEM_MMAP_MTRLOG_HDR_SIZE + n;
  //size_t total_mmap_mtrlog_area_size = mmap_mtrlogbuf->size; 
  // TODO(jhpark): how to clear mtr log records in mtr logging region
  //if ( (total_mmap_mtrlog_area_size - offset) < needed_space ) {
  //  fprintf(stderr, "[JONGQ] clear mtr log region\n");
  //  offset = 0;
  //}

#ifdef UNIV_LOG_HEADER
  // log << header << persist(log) << payload << persist(log)
  // (jhpark) : header version brought from 
  //            [Persistent Memory I/O Primitives](https://arxiv.org/pdf/1904.01614.pdf)
  
  // write header + payload
  volatile uint64_t org_offset = offset;
  memcpy(pdata+offset, mmap_mtr_hdr, (size_t)PMEM_MMAP_MTRLOG_HDR_SIZE);
  offset += PMEM_MMAP_MTRLOG_HDR_SIZE;
  memcpy(pdata+offset, buf, (size_t)n);
  
  // debug
  fprintf(stderr, "[JONGQ] offset: %lu size: %lu lsn: %lu\n", 
      offset, n, lsn);

  // persistent barrier
  flush_cache(pdata+org_offset, (size_t)(PMEM_MMAP_MTRLOG_HDR_SIZE + n));
  // jhpark: ALWAYS write offset of current mtr log.
	//				 In recovery, we must starts to read from this offset.
	//				 Also, maintain cur_offset in mtr_sys object to track the "next_write_to_offset"
  pm_mmap_write_logfile_header_size(org_offset);
  mmap_mtrlogbuf->cur_offset = offset + n;

#elif UNIV_LOG_ZERO
  // log << header << cnt << payload << persist(log)
  volatile int org_offset = offset;
  memcpy(pdata+offset, mmap_mtr_hdr, (size_t)PMEM_MMAP_MTRLOG_HDR_SIZE);
  offset += PMEM_MMAP_MTRLOG_HDR_SIZE;
  int cnt = __builtin_popcount((uint64_t)buf);
  memcpy(pdata+offset, &cnt, sizeof(cnt));
  offset += sizeof(cnt);
  memcpy(pdata+offset, buf, (size_t)n);
  // persistent barrier
  flush_cache(pdata+org_offset, (size_t)(PMEM_MMAP_MTRLOG_HDR_SIZE+sizeof(cnt)+n));
  mmap_mtrlogbuf->cur_offset = offset + n;

  // persistent barrier (for now, just ignore)
  //flush_cache(&mmap_mtrlogbuf->cur_offset, sizeof(mmap_mtrlogbuf->cur_offset));
#endif

  free(mmap_mtr_hdr);
  ret = n;
  return ret;
}
