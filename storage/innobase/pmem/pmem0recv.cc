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

  is_pmem_recv = false;
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

    uint64_t cur_hdr_offset = offset;
    offset += sizeof(PMEM_MMAP_MTRLOG_HDR);
    
    ulint ret = wrap_recv_parse_log_rec(&type, gb_pm_mmap + offset
        , gb_pm_mmap + offset + mtr_recv_hdr.len
        , &space, &page_no, false, &body);


    fprintf(stderr, "[DEBUG] ret: %lu type: %d space: %lu pange_no: %lu\n",ret, type, space ,page_no);

    // apply log records
    // HOT DEBUG we skip applying now !!
    
//    if (mtr_recv_hdr.need_recv) {
//      pmem_recv_recover_page_func(space, page_no, cur_hdr_offset);
//    }

    offset += mtr_recv_hdr.len;
  }

  // HOT DEBUG 3 // 
  // copy 4GB+2GB
  //memcpy(gb_pm_mmap + (4*1024*1024*1024UL), gb_pm_buf, (2*1024*1024*1024UL));

  fprintf(stderr, "[DEBUG] mtr log applying is finished!\n");
  is_pmem_recv=true;
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

bool pm_mmap_recv_check_nc_buffer(uint64_t space, uint64_t page_no) {
  
  uint64_t cur_offset = 0;
  uint64_t tmp_space, tmp_page_no;
  unsigned char *addr = gb_pm_mmap + (4*1024*1024*1024UL);
  uint64_t page_num_chunks = static_cast<uint64_t>( (2*1024*1024*1024UL)/4096);
  bool flag = false;

  for (uint64_t i=0; i<page_num_chunks; ++i) {

    tmp_space = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.space();
    tmp_page_no = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.page_no();

    if (!(tmp_space == 27 || tmp_space == 29)) {
      continue;
    }

    if (tmp_space == space 
        && tmp_page_no == page_no) {

      flag = true;
      unsigned char *frame = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->frame;

      fprintf(stderr, "[DEBUG] (%lu:%lu) (3) page exists in temp NVDIMM Buffer! pageLsn: %u i: %lu\n"
          ,space, page_no, mach_read_from_4(reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->frame+FIL_PAGE_LSN), i);

    /*
    fil_space_t* space_t = fil_space_get(space);
    const page_id_t page_id(space,page_no);
    const page_size_t page_size(space_t->flags);
    if (buf_page_is_corrupted(true, frame, page_size,
          fsp_is_checksum_disabled(space))) {
      fprintf(stderr, "(%lu:%lu) page is corruptted!\n", space, page_no);
    } else {
      fprintf(stderr, "(%lu:%lu) page is good!\n", space, page_no);
    }
    */
  
    }

  }
  return flag;
}

// fill map
void pm_mmap_recv_prepare() {

    ulint offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;
    while (true) {
      PMEM_MMAP_MTRLOG_HDR mtr_recv_hdr;
      memcpy(&mtr_recv_hdr, gb_pm_mmap+offset, sizeof(PMEM_MMAP_MTRLOG_HDR));
      if (mtr_recv_hdr.len == 0) break;

      pmem_nc_log_map[std::make_pair(mtr_recv_hdr.space, mtr_recv_hdr.page_no)].push_back(offset);

      offset += sizeof(PMEM_MMAP_MTRLOG_HDR);
      offset += mtr_recv_hdr.len;
    }

    // buffer
    uint64_t space, page_no;
    unsigned char *addr = gb_pm_mmap + (1*1024*1024*1024UL);
    uint64_t page_num_chunks = static_cast<uint64_t>( (2*1024*1024*1024UL)/4096);

    for (uint64_t i=0 ; i< page_num_chunks; ++i) {

      space = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.space();
      page_no = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.page_no();
      unsigned char *frame = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->frame;
      if (space != 27 && space != 29) continue;
      if (page_no > 1000000) continue;

      pmem_nc_buffer_map[std::make_pair(space,page_no)].push_back(i*sizeof(buf_block_t));
   }
 
}

bool pm_mmap_recv_check_nc_log(uint64_t space, uint64_t page_no) {
  std::map<std::pair<uint64_t,uint64_t>, std::vector<uint64_t> >::iterator ncbuf_iter;
  ncbuf_iter = pmem_nc_buffer_map.find(std::make_pair(space,page_no));
  if (ncbuf_iter != pmem_nc_buffer_map.end()) {
    return true;
  } else {
    return false;
  }


}

#ifdef UNIV_NVDIMM_CACHE
void pm_mmap_recv_flush_buffer() {

  uint64_t cur_offset = 0;
  uint64_t space, page_no;
  unsigned char *addr = gb_pm_mmap + (1*1024*1024*1024UL);
  uint64_t page_num_chunks = static_cast<uint64_t>( (2*1024*1024*1024UL)/4096);

  for (uint64_t i=0 ; i< page_num_chunks; ++i) {

    space = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.space();
    page_no = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.page_no();
    unsigned char *frame = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->frame;
    if (space != 27 && space != 29) continue;
    //if (frame == NULL) break;
    if (page_no > 1000000) continue;
 
    fprintf(stderr, "[DEBUG] (%lu:%lu) (3) page exists in temp NVDIMM Buffer! pageLsn: %u i: %lu\n"
        ,space, page_no, mach_read_from_4(reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->frame+FIL_PAGE_LSN), i);
   
    // (jhpark): check mtr log and determine recovery
    ulint offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;
 
    uint64_t x_lock_cnt = rw_lock_get_x_lock_count(&(reinterpret_cast<buf_block_t*>(addr+ i * sizeof(buf_block_t))->lock)); 
    uint64_t sx_lock_cnt = rw_lock_get_sx_lock_count(&(reinterpret_cast<buf_block_t*>(addr+ i * sizeof(buf_block_t))->lock)); 

    fprintf(stderr, "[DEBUG] (%lu:%lu) (3) page exists in temp NVDIMM Buffer! pageLsn: %u i: %lu lock(X,SX): %d:%d\n"
        ,space, page_no, mach_read_from_4(reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->frame+FIL_PAGE_LSN), i
        ,x_lock_cnt,sx_lock_cnt);

    fil_space_t* space_t = fil_space_get(space);
    const page_id_t page_id(space,page_no);
    const page_size_t page_size(space_t->flags);
    if (buf_page_is_corrupted(true, frame, page_size,
          fsp_is_checksum_disabled(space))) {
      fprintf(stderr, "(%lu:%lu) page is corruptted!\n", space, page_no);
    } else {
      fprintf(stderr, "(%lu:%lu) page is good!\n", space, page_no);
    }
  }
}
#endif

void pm_mmap_flush_nc_buffer() {
  // read from nc dummy buffer
  uint64_t cur_offset = 0;
  uint64_t space, page_no, page_lsn;

  //unsigned char * cur_tmp_buf = gb_pm_mmap + 4*1024*1024*1024UL;
  //uint64_t page_num_chunks = static_cast<uint64_t>( (2*1024*1024*1024UL)/4096);

  unsigned char *addr = gb_pm_buf;
  uint64_t page_num_chunks = static_cast<uint64_t>( (2*1024*1024*1024UL)/4096);


  is_pmem_recv=false;
  for (uint64_t i=0 ; i< page_num_chunks; ++i) {

    space = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.space();
    page_no = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.page_no();
    unsigned char *frame = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->frame;
    if (space != 27 && space != 29) continue;
    //if (frame == NULL) break;
    if (page_no > 1000000) continue;
 
    fprintf(stderr, "[DEBUG] (%lu:%lu) (3) page exists in temp NVDIMM Buffer! pageLsn: %u i: %lu\n"
        ,space, page_no, mach_read_from_4(reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->frame+FIL_PAGE_LSN), i);

   
    // (jhpark): check mtr log and determine recovery
    ulint offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;

    while (true) {
      PMEM_MMAP_MTRLOG_HDR mtr_recv_hdr;
      memcpy(&mtr_recv_hdr, gb_pm_mmap+offset, sizeof(PMEM_MMAP_MTRLOG_HDR));
      if (mtr_recv_hdr.len == 0) break;

      if (mtr_recv_hdr.space == space 
          && mtr_recv_hdr.page_no == page_no) {
        fprintf(stderr, "[DEBUG] --recovery  mtr log header check need_recv: %d len: %lu lsn: %lu mtr_lsn: %lu prev_offset: %lu space: %lu page: %lu\n"
            ,mtr_recv_hdr.need_recv, mtr_recv_hdr.len, mtr_recv_hdr.lsn, mtr_recv_hdr.mtr_lsn
            ,mtr_recv_hdr.prev_offset, mtr_recv_hdr.space, mtr_recv_hdr.page_no);


        if (mtr_recv_hdr.need_recv) { 
          buf_block_t *tmp_block;
          fil_space_t* space_t = fil_space_get(space);
          const page_id_t cur_page_id(space,page_no);
          const page_size_t cur_page_size(space_t->flags);
          mtr_t recv_mtr;
          mtr_start(&recv_mtr);
          buf_block_t* cur_block = buf_page_get(cur_page_id, cur_page_size, RW_NO_LATCH,
              &recv_mtr);

          uint64_t cur_pageLsn = mach_read_from_8(cur_block->frame + FIL_PAGE_LSN);
          fprintf(stderr, "[DEBUG] check (%u:%u) real: (%u:%u) cur_page_lsn:%u\n", space, page_no
            ,cur_block->page.id.space(), cur_block->page.id.page_no(), cur_pageLsn);


          if (cur_pageLsn <= mtr_recv_hdr.lsn) {
            pmem_recv_recover_page_func(space, page_no, offset, cur_block);
            mtr_recv_hdr.need_recv = false;
            memcpy(gb_pm_mmap+offset, &mtr_recv_hdr, sizeof(PMEM_MMAP_MTRLOG_HDR));
          }
        }

      }

      offset += sizeof(PMEM_MMAP_MTRLOG_HDR);
      offset += mtr_recv_hdr.len;
    }
  

  // page write test
    fil_space_t* space_t = fil_space_acquire_silent(space); 
    const page_id_t page_id(space,page_no);
    const page_size_t page_size(space_t->flags);
    IORequest write_request(IORequest::WRITE);
    write_request.disable_compression();
    int check = fil_io(write_request, true, page_id, page_size, 0
        , 4096, const_cast<byte*>(reinterpret_cast<buf_block_t*>(addr + i * sizeof(buf_block_t))->frame), NULL);
    fprintf(stderr, "fil_io check! %d\n", check);
  }


  //fprintf(stderr, "[DEBUG] (%lu:%lu) (3) page exists in temp NVDIMM Buffer! pageLSN: %lu i: %lu (%lu)\n"
  //      ,space, page_no, page_lsn, i, i*4096);
  is_pmem_recv=true;
  
}

void pmem_recv_recvoer_nc_page() {

  uint64_t cur_offset = 0;
  uint64_t space, page_no;
  unsigned char *addr = gb_pm_mmap + (4*1024*1024*1024UL);
  uint64_t page_num_chunks = static_cast<uint64_t>( (2*1024*1024*1024UL)/4096);

  for (uint64_t i=0 ; i< page_num_chunks; ++i) {

    //space = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.space();
    //page_no = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.page_no();
    //unsigned char *frame = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->frame;


    space = mach_read_from_4(addr+i*4096 + FIL_PAGE_SPACE_ID);
    page_no = mach_read_from_4(addr+i*4096 + FIL_PAGE_OFFSET);
    unsigned char *frame = (addr+i*4096);

    if (space != 27 && space != 29) continue;
    if (page_no > 1000000) continue;
 
    fprintf(stderr, "[DEBUG] (%lu:%lu) (3) page exists in temp NVDIMM Buffer! i: %lu\n"
        ,space, page_no, i);
   
    fil_space_t* space_t = fil_space_get(space);
    const page_id_t page_id(space,page_no);
    const page_size_t page_size(space_t->flags);
    if (buf_page_is_corrupted(true, frame, page_size,
          fsp_is_checksum_disabled(space))) {
      fprintf(stderr, "(%lu:%lu) page is corruptted!\n", space, page_no);
    } else {
      fprintf(stderr, "(%lu:%lu) page is good!\n", space, page_no);
    }

    // keep page info
    //pmem_nc_buffer_map[std::make_pair(space,page_no)].push_back(i*4096);

    // flush NC page
    /*
    IORequest write_request(IORequest::WRITE);
    write_request.disable_compression();
    int check = fil_io(write_request, true, page_id, page_size, 0
         ,4096, frame, NULL);
    fprintf(stderr, "fil_io check! %d\n", check);
    */
  }

  unsigned char *real_addr = gb_pm_mmap + (6*1024*1024*1024UL);
  uint64_t real_page_num_chunks = static_cast<uint64_t>( (2*1024*1024*1024UL)/4096);

  for (uint64_t i=0 ; i< real_page_num_chunks; ++i) {
    space = mach_read_from_4(real_addr+i*4096 + FIL_PAGE_SPACE_ID);
    page_no = mach_read_from_4(real_addr+i*4096 + FIL_PAGE_OFFSET);
    unsigned char *frame = (real_addr+i*4096);

    if (space != 27 && space != 29) continue;
    if (page_no > 1000000) continue;
 
    fprintf(stderr, "[DEBUG] (%lu:%lu) (3) page exists in real NVDIMM Buffer! i: %lu\n"
        ,space, page_no, i);
 
    /*
    std::map<std::pair<uint64_t,uint64_t>, std::vector<uint64_t> >::iterator ncbuf_iter;
    ncbuf_iter = pmem_nc_buffer_map.find(std::make_pair(space,page_no));
    if (ncbuf_iter != pmem_nc_buffer_map.end()) {
      uint64_t temp_offset = ncbuf_iter->second.back();
     } else {
      fprintf(stderr, "[DEBUG] hmm we do not have original copy! (%u:%u)\n", space, page_no);
    }
    */

      IORequest write_request(IORequest::WRITE);
      write_request.disable_compression();
      frame = gb_pm_mmap + (6*1024*1024*1024UL) + i * 4096;
      fil_space_t* space_t = fil_space_get(space);
      const page_id_t page_id(space,page_no);
      const page_size_t page_size(space_t->flags);
       pmem_nc_buffer_map[std::make_pair(space,page_no)].push_back(i*4096);
      
       int check = fil_io(write_request, true, page_id, page_size, 0
           ,4096, frame, NULL);
      fprintf(stderr, "fil_io check! %d\n", check);


  }

}
