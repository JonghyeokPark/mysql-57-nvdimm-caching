#include "pmem_mmap_obj.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stddef.h>

#include "ut0mutex.h"
#include "sync0types.h"
#include "mtr0log.h"
#include "trx0undo.h"

// gloabl persistent memmory region
unsigned char* gb_pm_mmap;
int gb_pm_mmap_fd;
PMEM_MMAP_MTRLOG_BUF* mmap_mtrlogbuf = NULL;

// recovery
bool is_pmem_recv = false;
uint64_t pmem_recv_offset = 0;
uint64_t pmem_recv_size = 0;

// jhpark-validate
std::map<std::pair<unsigned long, unsigned long>, uint64_t> nc_active_mtrlog_map;

unsigned char* pm_mmap_create(const char* path, const uint64_t pool_size) {
  
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

    gb_pm_mmap = (unsigned char *) mmap(NULL, pool_size, PROT_READ|PROT_WRITE, MAP_SHARED, gb_pm_mmap_fd, 0);
    if (gb_pm_mmap == MAP_FAILED) {
      PMEMMMAP_ERROR_PRINT("pm_mmap mmap() failed\n");
    }
    memset(gb_pm_mmap, 0x00, pool_size);

  } else {
    // TODO(jhaprk) add the recovery logic
    PMEMMMAP_INFO_PRINT("Start mtr recvoery process\n");
    gb_pm_mmap_fd = open(path, O_RDWR, 0777);

    if (gb_pm_mmap_fd < 0) {
      PMEMMMAP_ERROR_PRINT("pm_mmap_file open failed\n");
      return NULL;
    }

    gb_pm_mmap = (unsigned char *) mmap(NULL, pool_size, PROT_READ|PROT_WRITE, MAP_SHARED, gb_pm_mmap_fd, 0);
    if (gb_pm_mmap == MAP_FAILED) {
      PMEMMMAP_ERROR_PRINT("pm_mmap mmap() faild recovery failed\n");
    }
    is_pmem_recv=true;	
		
    // get file construct
		PMEM_MMAP_MTRLOGFILE_HDR* recv_mmap_mtrlog_fil_hdr = (PMEM_MMAP_MTRLOGFILE_HDR*) 
																													malloc(PMEM_MMAP_LOGFILE_HEADER_SZ);
		pm_mmap_read_logfile_header(recv_mmap_mtrlog_fil_hdr);	
			
		// debug
		fprintf(stderr, "[check] size: %lu, lsn: %lu, ckpt_lsn: %lu, ckpt_offset: %lu\n",
						recv_mmap_mtrlog_fil_hdr->size, recv_mmap_mtrlog_fil_hdr->flushed_lsn, 
						recv_mmap_mtrlog_fil_hdr->ckpt_lsn, recv_mmap_mtrlog_fil_hdr->ckpt_offset);

		// recvoery check : read from checkpoint offset
    PMEM_LOG_HDR* recv_mmap_mtrlog_hdr = (PMEM_LOG_HDR*) malloc(PMEM_LOG_HDR_SZ);
    memcpy(recv_mmap_mtrlog_hdr, gb_pm_mmap+recv_mmap_mtrlog_fil_hdr->ckpt_offset, PMEM_LOG_HDR_SZ);
		 
//		if (recv_mmap_mtrlog_fil_hdr->size == PMEM_MMAP_MTR_FIL_HDR_SIZE 
//				|| recv_mmap_mtrlog_hdr->need_recv == 0) {
   
    if (recv_mmap_mtrlog_fil_hdr->size == PMEM_MMAP_MTR_FIL_HDR_SIZE) {
			PMEMMMAP_INFO_PRINT("Normal Shutdown case, don't need to recveory; Recovery process is terminated\n");
		} else {
			// TODO(jhpark): real recovery process
			is_pmem_recv = true;
			pmem_recv_offset = pm_mmap_recv_check(recv_mmap_mtrlog_fil_hdr);
			pmem_recv_size = recv_mmap_mtrlog_fil_hdr->size;		

      // jhpark: GUESS, before flush NC page , need to recovery first !!!
			//pm_mmap_recv_flush_buffer();
			PMEMMMAP_INFO_PRINT("recovery offset: %lu\n", pmem_recv_offset);
		} 
  
    // DEBUG 
    pm_for_debug_REDO(recv_mmap_mtrlog_fil_hdr->size);

		// step1. allocate mtr_recv_sys
		// step2. 1) get header infor mation and 2) get info from mtr log region
		// step3. reconstruct undo page

    // Get header information from exsiting nvdimm log file
    // need to check NC logs from checkpoint offset 
    // TODO(jhaprk): consider using prev_offset
    //memset(recv_mmap_mtrlog_hdr, 0x00, PMEM_MMAP_MTRLOG_HDR_SIZE);
    //memcpy(recv_mmap_mtrlog_hdr, gb_pm_mmap+recv_prev_offset, PMEM_MMAP_MTRLOG_HDR_SIZE);

    // debug
    fprintf(stderr, "len: %lu\n", recv_mmap_mtrlog_hdr->len);
    fprintf(stderr, "lsn: %lu\n", recv_mmap_mtrlog_hdr->lsn);
    fprintf(stderr, "need_recovery: %d\n", recv_mmap_mtrlog_hdr->need_recv);
    
		free(recv_mmap_mtrlog_fil_hdr);
    free(recv_mmap_mtrlog_hdr);
  
  } // end-of-else

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
    free(mmap_mtrlogbuf);
    mmap_mtrlogbuf = NULL;
  }
}

void pm_mmap_free(const uint64_t pool_size) {
	// free mtrMutex 
	pthread_mutex_destroy(&mmap_mtrlogbuf->mtrMutex);
	munmap(gb_pm_mmap, pool_size);
	close(gb_pm_mmap_fd);
	// free mtr log system
	pm_mmap_mtrlogbuf_mem_free();
	PMEMMMAP_INFO_PRINT("munmap persistent memroy region\n");
}


// allocate mtr log buffer
int pm_mmap_mtrlogbuf_init(const size_t size) {

  mmap_mtrlogbuf = static_cast<PMEM_MMAP_MTRLOG_BUF*>(malloc(sizeof(PMEM_MMAP_MTRLOG_BUF)));

	// TODO(jhpark): add mcs-lock version
	pthread_mutex_init(&mmap_mtrlogbuf->mtrMutex, NULL);
  mmap_mtrlogbuf->size = size;
  mmap_mtrlogbuf->cur_offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;
	mmap_mtrlogbuf->prev_offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;
	mmap_mtrlogbuf->ckpt_offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;
  mmap_mtrlogbuf->min_invalid_offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;
  mmap_mtrlogbuf->min_invalid_offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;

  PMEMMMAP_INFO_PRINT("MTR LOG BUFFER structure initialization finished!\n"); 
  return 0;
}

// get mtr log header information
void pm_mmap_read_logfile_header(PMEM_MMAP_MTRLOGFILE_HDR* mtrlog_fil_hdr) {
	mtrlog_fil_hdr->size = mach_read_from_8(gb_pm_mmap + PMEM_MMAP_MTR_FIL_HDR_SIZE_OFFSET);
	mtrlog_fil_hdr->flushed_lsn = mach_read_from_8(gb_pm_mmap + PMEM_MMAP_MTR_FIL_HDR_LSN);
	mtrlog_fil_hdr->ckpt_lsn = mach_read_from_8(gb_pm_mmap + PMEM_MMAP_MTR_FIL_HDR_CKPT_LSN);
	mtrlog_fil_hdr->ckpt_offset = mach_read_from_8(gb_pm_mmap + PMEM_MMAP_MTR_FIL_HDR_CKPT_OFFSET);
}

// write header offset (i.e., size) of latest mtr log
void pm_mmap_write_logfile_header_size(uint64_t size) {
  byte hdr_size[8];
  mach_write_to_8(hdr_size, size);
  memcpy(gb_pm_mmap + PMEM_MMAP_MTR_FIL_HDR_SIZE_OFFSET, hdr_size, 8);
	flush_cache(gb_pm_mmap + PMEM_MMAP_MTR_FIL_HDR_SIZE, 8);
}

// write oldeset_lsn of NVDIMM-caching page when NVDIMM-caching page is flushed 
void pm_mmap_write_logfile_header_lsn(uint64_t lsn) {
	byte hdr_lsn[8];
	mach_write_to_8(hdr_lsn, (uint64_t)lsn);
	memcpy(gb_pm_mmap + PMEM_MMAP_MTR_FIL_HDR_LSN, hdr_lsn, 8);
	flush_cache(gb_pm_mmap + PMEM_MMAP_MTR_FIL_HDR_LSN, 8);
}

// write checkpoint info 
void pm_mmap_write_logfile_header_ckpt_info(uint64_t offset, uint64_t lsn) {
	byte ckpt_offset[8], ckpt_lsn[8];
	mach_write_to_8(ckpt_offset, offset);
	mach_write_to_8(ckpt_lsn, (uint64_t)lsn);
	memcpy(gb_pm_mmap + PMEM_MMAP_MTR_FIL_HDR_CKPT_LSN, ckpt_lsn, 8);
	memcpy(gb_pm_mmap + PMEM_MMAP_MTR_FIL_HDR_CKPT_OFFSET, ckpt_offset, 8);
	flush_cache(gb_pm_mmap + PMEM_MMAP_MTR_FIL_HDR_CKPT_LSN, 16);
}

// (eager) checkpoint mtr log return ckpt_offset
void pm_mmap_log_commit(ulint cur_space, ulint cur_page, ulint cur_offset) {
	
	size_t offset = mmap_mtrlogbuf->ckpt_offset;
	bool ckpt_flag = true;

	while (true) {

		if (offset >= cur_offset) {
			break;
		}

		PMEM_MMAP_MTRLOG_HDR *tmp_mmap_hdr = (PMEM_MMAP_MTRLOG_HDR *) malloc(PMEM_MMAP_MTRLOG_HDR_SIZE);
		memset(tmp_mmap_hdr, 0, sizeof(PMEM_MMAP_MTRLOG_HDR));
		memcpy(tmp_mmap_hdr, gb_pm_mmap + offset, PMEM_MMAP_MTRLOG_HDR_SIZE);
		ut_ad(tmp_mmap_hdr == NULL);

		// (jhpark): At this point, call checkpoint and then call commit
		// no need to commit
		if (tmp_mmap_hdr->len == 0) {
			break;
		}	
	
		if(tmp_mmap_hdr->need_recv == 0) {
			mmap_mtrlogbuf->ckpt_offset = offset;
			offset += PMEM_MMAP_MTRLOG_HDR_SIZE + tmp_mmap_hdr->len;
			continue;
		}

		if (cur_space == tmp_mmap_hdr->space &&
				cur_page == tmp_mmap_hdr->page_no) {

			// 1. unset recovery flag
			bool val = false;
			memcpy(gb_pm_mmap + offset, &val, (size_t)offsetof(PMEM_MMAP_MTRLOG_HDR, len));
			// 2. keep ckpt_offset (continuous)
			offset += PMEM_MMAP_MTRLOG_HDR_SIZE + tmp_mmap_hdr->len;

			if (ckpt_flag) {
				mmap_mtrlogbuf->ckpt_offset = offset;
				// write ckpt info (lsn parameter is unused)
				//fprintf(stderr, "write ckpt info offset: %lu\n", offset);
				pm_mmap_write_logfile_header_ckpt_info(offset, 0);
			}
//			fprintf(stderr, "[mtr-commit] YES ckpt_offset: %lu space: %lu page_no: %lu\n"
//							,mmap_mtrlogbuf->ckpt_offset,tmp_mmap_hdr->space, tmp_mmap_hdr->page_no);
		} else {
//			fprintf(stderr, "[mtr-commit] NO space: %lu page_no: %lu\n"
//							,tmp_mmap_hdr->space, tmp_mmap_hdr->page_no);
			
			offset += PMEM_MMAP_MTRLOG_HDR_SIZE + tmp_mmap_hdr->len;
			ckpt_flag = false;
		}
		// free tmp_mmap_hdr
		free(tmp_mmap_hdr);
	}	
}

// write mtr log
ssize_t pm_mmap_mtrlogbuf_write( 
    const uint8_t* buf,
    unsigned long int n,
    unsigned long int lsn) 
{
  unsigned long int ret = 0;

	pthread_mutex_lock(&mmap_mtrlogbuf->mtrMutex);

  // (jhpark): original version of mtr logging relies on external offset value
  //           which is persisted on every updates
  //           cur_offset points offset which header will be appended
  if (mmap_mtrlogbuf == NULL) {
    PMEMMMAP_ERROR_PRINT("mmap_mtrlogbuf is NULL!\n");
		return -1;
  }

  // jhpark: Force to trigger mtr_log_checkpoint if the offset becomes larger than threshold
	size_t offset = mmap_mtrlogbuf->cur_offset;
	int limit = mmap_mtrlogbuf->size * 0.75;

	// checkopint occurred
  if (offset + n > limit) {
		// debug
		//PMEMMMAP_INFO_PRINT("[WRNING] mmap_mtrlogbuf is FULL!\n");
		ut_ad(offset < mmap_mtrlogbuf->size);

		offset = pm_mmap_log_checkpoint(offset);
		mmap_mtrlogbuf->prev_offset = offset;
		mmap_mtrlogbuf->cur_offset = offset;
	}

	// Fill the mmap_mtr log header info.
  PMEM_MMAP_MTRLOG_HDR* mmap_mtr_hdr = (PMEM_MMAP_MTRLOG_HDR*) malloc(PMEM_MMAP_MTRLOG_HDR_SIZE);
  mmap_mtr_hdr->len = n;
  // lsn from log_sys
  mmap_mtr_hdr->lsn = lsn;
  // lsn from mtr_log_sys
  //mmap_mtr_hdr->mtr_lsn = mmap_mtrlogbuf->mtr_sys_lsn+1;
	//mmap_mtrlogbuf->mtr_sys_lsn++;
	// prev hdr offset
	mmap_mtr_hdr->prev_offset = mmap_mtrlogbuf->prev_offset;
	// set recv_flag
	mmap_mtr_hdr->need_recv = 1;

#ifdef UNIV_LOG_HEADER
	// log << header << persist(log) << payload << persist(log)
 	// (jhpark) : header version brought from 
 	//            [Persistent Memory I/O Primitives](https://arxiv.org/pdf/1904.01614.pdf)
 
	// check mtr log type
	const byte *ptr = buf;
	const byte *end_ptr = buf+n;
	ulint cur_space = 0, cur_page = 0;
	mlog_id_t type;

	type = (mlog_id_t)((ulint)*ptr & ~MLOG_SINGLE_REC_FLAG);
	ptr++;
	cur_space = mach_parse_compressed(&ptr, end_ptr);
	if (ptr != NULL) {
		cur_page = mach_parse_compressed(&ptr, end_ptr);
		//fprintf(stderr, "mtr log type: %lu space: %lu page: %lu\n", type, cur_space, cur_page);
	}

	mmap_mtr_hdr->space = cur_space;
	mmap_mtr_hdr->page_no = cur_page;
 
  // write header + payload
  uint64_t org_offset = offset;
  memcpy(gb_pm_mmap+offset, mmap_mtr_hdr, (size_t)PMEM_MMAP_MTRLOG_HDR_SIZE);
  offset += PMEM_MMAP_MTRLOG_HDR_SIZE;
  memcpy(gb_pm_mmap+offset, buf, n);

	// add space_id and page_no in the header
	if (type == MLOG_2BYTES) {
		ulint page_off = mach_read_from_2(ptr);
		ptr += 2;
		ulint state = mach_parse_compressed(&ptr, end_ptr);
		
		// At this point, we can remove mtr log that means transaction commit successfully.
		if (state >= TRX_UNDO_CACHED) {
			ut_ad(state != TRX_UNDO_PREPARED);

			// debug commit mtr log
			//fprintf(stderr, "[mtr-write] call commit state: %lu cur_space: %lu cur_page: %lu cur_offset: %lu\n"
			//				,state, cur_space, cur_page, org_offset);
			pm_mmap_log_commit(cur_space, cur_page, offset+n);
		} 
	} else {
		//fprintf(stderr, "[mtr-write] not commit cur_space: %lu cur_page: %lu cur_offset: %lu\n"
		//				 ,cur_space, cur_page, org_offset);
	}
	
  // persistent barrier
  flush_cache(gb_pm_mmap+org_offset, (size_t)(PMEM_MMAP_MTRLOG_HDR_SIZE + n));

  // jhpark: ALWAYS write offset of current mtr log.
	//				 In recovery, we must starts to read from this offset.
	//				 Also, maintain cur_offset in mtr_sys object to track the "next_write_to_offset"
  pm_mmap_write_logfile_header_size(org_offset);
	mmap_mtrlogbuf->prev_offset = mmap_mtrlogbuf->cur_offset;
  mmap_mtrlogbuf->cur_offset = offset + n;
  free(mmap_mtr_hdr);
#endif

/*
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
*/

	pthread_mutex_unlock(&mmap_mtrlogbuf->mtrMutex);
  ret = n;
  return ret;
}

// compare mtr log with given space_id, and page_no
// offset is start offset of "log body" of mtr log
bool pm_mmap_mtrlogbuf_identify(size_t offset, size_t n, ulint space, ulint page_no) {
	// mtr log structure: [type(1)] [space_id(4)] [page_no(4)]
	// <WARNING> mach_write_compressed used when writing space_id and page_no
	// + 1 means jump over MTR_LOG_TYPE
	ulint cur_space, cur_page;
	const byte *ptr = gb_pm_mmap+offset;
	const byte *end_ptr = gb_pm_mmap+offset+n;
	ptr++;
	cur_space = mach_parse_compressed(&ptr, end_ptr);
	if (ptr != NULL) {
		cur_page = mach_parse_compressed(&ptr, end_ptr);
	}
	//fprintf(stderr, "[mtr identify] space(%lu) : %lu pange_no(%lu) : %lu\n", space, cur_space, page_no, cur_page);
	return ((cur_space == space) && (cur_page == page_no));
}

void pm_mmap_mtrlogbuf_unset_recv_flag(size_t offset) {
	memcpy(gb_pm_mmap + offset, false, sizeof(bool));
	// need flush? No we can recover by using commit log
}

void pm_mmap_mtrlogbuf_commit_v1(ulint space, ulint page_no) {
	// 1. start to inspect mtr log from latest ckpt_offset
	// 2. check specific mtr log with spaced_id and page_no
	// 2.1 (yes) check need_recv is set goto 3.1
	// 2.2 (no) check need recv is set goto 3.2
	// 3.1. update ckpt_offset to current offset
	// 4. move to next mtr log (until cur_offset)
	
	if (mmap_mtrlogbuf == NULL) return;

	size_t offset = mmap_mtrlogbuf->ckpt_offset;
	while (offset != mmap_mtrlogbuf->cur_offset) {

		fprintf(stderr, "offset : %lu cur_offset: %lu\n", offset, mmap_mtrlogbuf->cur_offset);
		PMEM_MMAP_MTRLOG_HDR mmap_mtr_hdr;
		memcpy(&mmap_mtr_hdr, gb_pm_mmap + offset, (size_t) PMEM_MMAP_MTRLOG_HDR_SIZE);
		uint64_t data_len = mmap_mtr_hdr.len;
		unsigned int need_recv = mmap_mtr_hdr.need_recv;
	
		fprintf(stderr, "[mtr info] data_len : %lu lsn: %lu need_recv : %d\n", 
						data_len, mmap_mtr_hdr.lsn, need_recv);
	
		// move next
		uint64_t org_offset = offset;
		offset += PMEM_MMAP_MTRLOG_HDR_SIZE;

		if (pm_mmap_mtrlogbuf_identify(offset, data_len, space, page_no)) {
			pm_mmap_mtrlogbuf_unset_recv_flag(org_offset);
		}
		if (need_recv) {
			mmap_mtrlogbuf->ckpt_offset = org_offset;
		}
		offset += data_len;
	}
	fprintf(stderr, "break out ! ckpt_offset: %lu\n", mmap_mtrlogbuf->ckpt_offset);
}

// jhpark-recvoery
int pm_mmap_memcmp(const unsigned char *tmp1, const unsigned char *tmp2, unsigned long len) {

  if (len >= 4) {
    long diff = *(unsigned long *)tmp1 - *(unsigned long *)tmp2;
    if (diff) {
      return diff;
    }
  }

  return memcmp(tmp1,tmp2,len);
}






//////////////////////////// Hello ////////////////////////////////

void pm_mmap_mtrlogbuf_check() {
  uint64_t offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;
  fprintf(stderr, " ======================= check mtrlogbuf ==================== \n");
  while (true) {
    // TODO(jhpark): need to initialize the mtrlogbuffer first, but ignore for debugging
    if (offset >= (1024*1024*1024*1UL)) break;
    PMEM_MMAP_PLACEHOLDER body;
    memcpy(&body, gb_pm_mmap+offset, PMEM_MMAP_PLACEHODER_SZ);
    fprintf(stderr, "offset: %lu is_valid: %lu trx_id: %lu space: %lu page: %lu\n"
        ,offset, body.is_valid, body.trx_id, body.space_id, body.page_no);

    // add current page_id information 
    //nc_active_mtrlog_map[std::make_pair(body.space_id, body.page_no)] = body.trx_id;
    nc_active_mtrlog_map[std::make_pair(body.space_id, body.page_no)] = body.is_valid;

//    PMEM_MMAP_PLACEHOLDER* body = (PMEM_MMAP_PLACEHOLDER*) malloc(PMEM_MMAP_PLACEHODER_SZ);  
//    memcpy(body, gb_pm_mmap+offset, PMEM_MMAP_PLACEHODER_SZ);
//    fprintf(stderr, "offset: %lu is_valid: %lu trx_id: %lu space: %lu page: %lu\n"
//        ,offset, body->is_valid, body->trx_id, body->space_id, body->page_no);
//    free(body);
    offset += PMEM_MMAP_PLACEHODER_SZ;
  }
  fprintf(stderr, " ======================= check mtrlogbuf ==================== \n");
}

bool pm_mmap_mtrlogbuf_validate(unsigned long space_id, unsigned long page_no) {
  std::map<std::pair<unsigned long, unsigned long>, uint64_t>::iterator iter;
  iter = nc_active_mtrlog_map.find(std::make_pair(space_id, page_no));
  //return (iter != nc_active_mtrlog_map.end());

  if ( (iter != nc_active_mtrlog_map.end()) && nc_active_mtrlog_map[std::make_pair(space_id, page_no)]==0) {
    return true;
  } else {
    return false;
  }
}


uint64_t pm_mmap_log_checkpoint(uint64_t cur_offset) {
	///////////////////////////////////////////////////
	// recovery test
  //	PMEMMMAP_ERROR_PRINT("RECOVERY TEST !!!");
  //	exit(1);
	///////////////////////////////////////////////////
  pthread_mutex_lock(&mmap_mtrlogbuf->mtrMutex);
	size_t finish_offset = mmap_mtrlogbuf->ckpt_offset;
	// invalidate all offset;
  if (finish_offset != PMEM_MMAP_MTR_FIL_HDR_SIZE) {
    // zero checkpoint region
  	memset(gb_pm_mmap + PMEM_MMAP_MTR_FIL_HDR_SIZE, 0x00, 
              (finish_offset - PMEM_MMAP_MTR_FIL_HDR_SIZE));

    // copy existing offset;
	  if (cur_offset < finish_offset) {
		  PMEMMMAP_ERROR_PRINT("offset error at checkpoint!");
	  }

    // copy valid log portion
    memcpy(gb_pm_mmap + PMEM_MMAP_MTR_FIL_HDR_SIZE, 
           gb_pm_mmap + finish_offset, (cur_offset - finish_offset));

	  mmap_mtrlogbuf->ckpt_offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;
  }

		// debug	
	  fprintf(stderr, "[mtr-checkpoint] ckpt_offset: %lu, len: %lu\n", finish_offset, (cur_offset - finish_offset));

	 PMEM_MMAP_MTRLOG_HDR tmp_hdr;
	 memcpy(&tmp_hdr, gb_pm_mmap + mmap_mtrlogbuf->ckpt_offset, PMEM_MMAP_MTRLOG_HDR_SIZE);
	 fprintf(stderr, "[mtr-checkpoint] after checkpoint need_recv: %lu len: %lu lsn: %lu\n", tmp_hdr.need_recv, tmp_hdr.len, tmp_hdr.lsn);
    pthread_mutex_lock(&mmap_mtrlogbuf->mtrMutex);

	return PMEM_MMAP_MTR_FIL_HDR_SIZE + (cur_offset - finish_offset);
}


// commit mtr log 
// (jhpark)-#200904: this function guarantees NC page is valid
//                   it records transaction id and page_id
//                   for the simplicity, remove the header
//                   [size][space_id][page_no][trx_id]

void pm_mmap_mtrlogbuf_record(unsigned long space, unsigned long page_no, uint64_t trx_id) {
  
  // TODO(jhpark): mutex still needed?
  pthread_mutex_lock(&mmap_mtrlogbuf->mtrMutex);
  // (jhpark): original version of mtr logging relies on external offset value
  //           which is persisted on every updates
  //           cur_offset points offset which header will be appended
  if (mmap_mtrlogbuf == NULL) {
    PMEMMMAP_ERROR_PRINT("mmap_mtrlogbuf is NULL!\n");
		return;
  }
  size_t offset = mmap_mtrlogbuf->cur_offset;

  // check the capacity
  int limit = mmap_mtrlogbuf->size * 0.75;
  if (offset + PMEM_MMAP_PLACEHODER_SZ > limit) {
		PMEMMMAP_INFO_PRINT("[WRNING] mmap_mtrlogbuf is FULL!\n");
		ut_ad(offset < mmap_mtrlogbuf->size);
		offset = pm_mmap_log_checkpoint(offset);
	}

  // debug
  fprintf(stderr, "[MTR_RECORD] offset: %lu trx_id: %lu page:%lu:%lu\n", offset, trx_id, space, page_no);
  PMEM_MMAP_PLACEHOLDER* body = (PMEM_MMAP_PLACEHOLDER*) malloc(PMEM_MMAP_PLACEHODER_SZ);
  body->is_valid = 0;
  body->space_id = space; 
  body->page_no = page_no;
  body->trx_id = trx_id;

  uint64_t org_offset = offset;
  memcpy(gb_pm_mmap+offset, body, (size_t)PMEM_MMAP_PLACEHODER_SZ);
  offset += PMEM_MMAP_PLACEHODER_SZ;

  // persistent barrier
  flush_cache(gb_pm_mmap+org_offset, (size_t)PMEM_MMAP_PLACEHODER_SZ); 

   // jhpark: ALWAYS write offset of current mtr log.
	//				 In recovery, we must starts to read from this offset.
	//				 Also, maintain cur_offset in mtr_sys object to track the "next_write_to_offset"
  pm_mmap_write_logfile_header_size(org_offset);
  mmap_mtrlogbuf->cur_offset = offset; 
  free(body);
  pthread_mutex_unlock(&mmap_mtrlogbuf->mtrMutex);
}

void pm_mmap_mtrlogbuf_log_commit(unsigned long space, unsigned long page_no, uint64_t trx_id) {

  pthread_mutex_lock(&mmap_mtrlogbuf->mtrMutex);
  uint32_t found = 0;
  uint64_t offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;
  fprintf(stderr, "[MTR-COMMIT] START FIND OUT !!!! trx_id: %lu page_id(%lu:%lu)\n"
                 , trx_id, space, page_no);
  while (true) {
    if (offset >= mmap_mtrlogbuf->cur_offset) {
      break;
    }

    PMEM_MMAP_PLACEHOLDER body;
    memcpy(&body, gb_pm_mmap+offset ,PMEM_MMAP_PLACEHODER_SZ);
    // FIXME(jhpark): jhpark-recovery-3
    //if (body.trx_id == trx_id  &&
    
    if (body.page_no == page_no 
        && body.space_id == space) {
     
      if (body.trx_id == trx_id) {
        fprintf(stderr, "[MTR-COMMIT] trx_id: %lu page_id(%lu:%lu)\n"
           , trx_id, space, page_no);
        *(uint64_t*)(gb_pm_mmap+offset) = 1; 
        found++;
        // return;
      } else if (trx_id == 0) {
        fprintf(stderr, "[MTR-COMMIT] jhpark-recovery-3 trx_id: %lu page_id(%lu:%lu)\n"
            , trx_id, space, page_no);
        *(uint64_t*)(gb_pm_mmap + offset) = 1;
        found++;
      }

      // tune the range of checkpoint region
      if (mmap_mtrlogbuf->max_invalid_offset < offset) {
        mmap_mtrlogbuf->max_invalid_offset = offset;
      }

    }
    offset += PMEM_MMAP_PLACEHODER_SZ;
  } // end of while

  fprintf(stderr, "[MTR-COMMIT] escpae found? : %lu\n", found);
  pthread_mutex_unlock(&mmap_mtrlogbuf->mtrMutex);
}

void pm_mmap_mtrlogbuf_commit(unsigned char* rec, unsigned long cur_rec_size ,ulint space, ulint page_no) {
	// TODO(jhaprk): Keep page modification finish log for recovery	
	// For current mtr logging version, we jsut ignore this function
	flush_cache(rec, cur_rec_size);
  if (mmap_mtrlogbuf == NULL) return;
}




