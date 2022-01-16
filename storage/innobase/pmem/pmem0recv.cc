#include "pmem_mmap_obj.h"

#include <sys/mman.h>
#include <sys/stat.h>
#include <stdio.h>
#include <fcntl.h>
#include <stdlib.h>
#include <errno.h>
#include <stddef.h>

#include "mtr0log.h"
#include "buf0buf.h"
#include "log0recv.h"
#include "mtr0mtr.h"
#include "fil0fil.h"

#include "os0file.h"
#include "page0page.h"
#include "mtr0types.h"
#include "trx0rec.h"

#include "buf0rea.h"

extern unsigned char* gb_pm_mmap;
extern uint64_t pmem_recv_size;

void pm_mmap_recv(uint64_t start_offset, uint64_t end_offset) {
  // parse log records
  // TODO(jhpark): we need to use start_offset

  ulint offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;
  mlog_id_t type;
  ulint space, page_no;
  byte* body; 

  fprintf(stderr, "[DEBUG] mtr_log_recovery end_offset: %lu\n", end_offset);
//  while (offset <= end_offset) {
  while (1) {

    // read mtr log header 
    PMEM_MMAP_MTRLOG_HDR mtr_recv_hdr;
    memcpy(&mtr_recv_hdr, gb_pm_mmap+offset, sizeof(PMEM_MMAP_MTRLOG_HDR));
 
    if (mtr_recv_hdr.len == 0) {
      break;
    }

    fprintf(stderr, "[DEBUG] mtr log header check need_recv: %d len: %lu lsn: %lu mtr_lsn: %lu prev_offset: %lu space: %lu page: %lu\n"
        ,mtr_recv_hdr.need_recv, mtr_recv_hdr.len, mtr_recv_hdr.lsn, mtr_recv_hdr.mtr_lsn
        ,mtr_recv_hdr.prev_offset, mtr_recv_hdr.space, mtr_recv_hdr.page_no);

    offset += sizeof(PMEM_MMAP_MTRLOG_HDR);
    
    ulint ret = wrap_recv_parse_log_rec(&type, gb_pm_mmap + offset
        , gb_pm_mmap + offset + mtr_recv_hdr.len
        , &space, &page_no, false, &body);


    fprintf(stderr, "[DEBUG] ret: %lu type: %d space: %lu pange_no: %lu\n",ret, type, space ,page_no);

    // apply log records
    // HOT DEBUG we skip applying now !!
    /*
    if (mtr_recv_hdr.need_recv) {
      pmem_recv_recover_page_func(space, page_no);
    }
    */

    offset += mtr_recv_hdr.len;
  }

  fprintf(stderr, "[DEBUG] mtr log applying is finished!\n");
}


uint64_t pm_mmap_recv_check(PMEM_MMAP_MTRLOGFILE_HDR* log_fil_hdr) {
	size_t tmp_offset = log_fil_hdr->ckpt_offset;
	while (true) {
		fprintf(stderr, "current tmp_offset: %lu:(%lu)\n", tmp_offset, log_fil_hdr->size);
		if (tmp_offset >= log_fil_hdr->size) {
			break;
		}

		PMEM_MMAP_MTRLOG_HDR *recv_mmap_hdr = (PMEM_MMAP_MTRLOG_HDR *) malloc(PMEM_MMAP_MTRLOG_HDR_SIZE);
		memcpy(recv_mmap_hdr, gb_pm_mmap + tmp_offset, PMEM_MMAP_MTRLOG_HDR_SIZE);
		ut_ad(recv_mmap_hdr == NULL);

		fprintf(stderr, "[recovery] need_recv: %d len: %lu lsn: %lu prev_offset: %lu space: %lu page_no: %lu\n"
										,recv_mmap_hdr->need_recv, recv_mmap_hdr->len, recv_mmap_hdr->lsn,
										recv_mmap_hdr->prev_offset,
										recv_mmap_hdr->space, recv_mmap_hdr->page_no);

    if (recv_mmap_hdr->need_recv == false) {
      fprintf(stderr, "Hmm? current log doesn't need to recvoery!\n");
      tmp_offset += recv_mmap_hdr->len;
      free(recv_mmap_hdr);
      continue;
    } else {
			free(recv_mmap_hdr);
			return tmp_offset;
		}
	}
	// no need to recovery
  return -1;
}


void pm_mmap_recv_flush_buffer() {

  uint64_t cur_offset = 0;
  uint64_t total_buf_size = (1024*1024*1024*2UL);
  unsigned char* cur_gb_pm_buf = gb_pm_mmap + (1024*1024*1024*2UL);
  uint64_t space, page_no;

  while (true) {
     if (cur_offset >= total_buf_size) {
       break;
     }

     space = reinterpret_cast<buf_block_t*>(cur_gb_pm_buf)->page.id.space();
     page_no = reinterpret_cast<buf_block_t*>(cur_gb_pm_buf)->page.id.page_no();
     byte* frame = reinterpret_cast<buf_block_t*>(cur_gb_pm_buf)->frame;

     byte check_page[4096] = {0,};

     fprintf(stderr, "[DEBUG] (%lu:%lu) page exists in NVDIMM Buffer (%lu:%lu)\n"
         , space, page_no);

     cur_gb_pm_buf += sizeof(buf_block_t); 
  }

}

