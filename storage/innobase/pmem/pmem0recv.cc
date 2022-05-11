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

// fill map
void pm_mmap_recv_prepare() {

    // buffer
    uint64_t space, page_no;
    unsigned char *addr = gb_pm_mmap + (1*1024*1024*1024UL);
    uint64_t page_num_chunks = static_cast<uint64_t>( (srv_nvdimm_buf_pool_size)/4096);

    for (uint64_t i=0 ; i< page_num_chunks; ++i) {

      space = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.space();
      page_no = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.page_no();
      unsigned char *frame = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->frame;
      if (space != 27 && space != 29) continue;
      if (page_no > 1000000) continue;

      pmem_nc_buffer_map[std::make_pair(space,page_no)].push_back(i*sizeof(buf_block_t));
   }
 
}

uint64_t pm_mmap_recv_check_nc_log(uint64_t space, uint64_t page_no) {
  std::map<std::pair<uint64_t,uint64_t>, std::vector<uint64_t> >::iterator nclog_iter;
  nclog_iter = pmem_nc_log_map.find(std::make_pair(space,page_no));
  if (nclog_iter != pmem_nc_log_map.end()) { 
    std::vector<uint64_t> nc_offset_vec = (*nclog_iter).second;
    uint64_t nc_offset;
    for (uint64_t i=0; i<nc_offset_vec.size(); i++) {
     nc_offset = nc_offset_vec[i];

     PMEM_MMAP_MTRLOG_HDR mtr_recv_hdr;
     memcpy(&mtr_recv_hdr, gb_pm_mmap+nc_offset, sizeof(PMEM_MMAP_MTRLOG_HDR));

     fprintf(stderr, "[DEBUG] NC LOG (%lu:%lu) offset: %lu page_lsn: %lu i: %d type: %d\n", 
          space, page_no, mach_read_from_8(gb_pm_mmap + nc_offset + sizeof(PMEM_MMAP_MTRLOG_HDR) + FIL_PAGE_LSN), i, mtr_recv_hdr.need_recv);
    }

    PMEM_MMAP_MTRLOG_HDR mtr_recv_hdr;
    memcpy(&mtr_recv_hdr, gb_pm_mmap+nc_offset, sizeof(PMEM_MMAP_MTRLOG_HDR));
    //if (mtr_recv_hdr.type == 2) return -1; 
    fprintf(stderr, "[DEBUG] NC LOG (%lu:%lu) need_recv: %d\n", space, page_no, mtr_recv_hdr.need_recv);

    return nc_offset;
  } else {
    return -1;
  }


}

#ifdef UNIV_NVDIMM_CACHE
void pm_mmap_recv_flush_buffer() {

  uint64_t cur_offset = 0;
  uint64_t space, page_no;
  unsigned char *addr = gb_pm_mmap + (1*1024*1024*1024UL);
  uint64_t page_num_chunks = static_cast<uint64_t>( (srv_nvdimm_buf_pool_size)/4096);

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

void pmem_recv_recvoer_nc_page() {

  uint64_t cur_offset = 0;
  uint64_t space, page_no;
  unsigned char *addr = gb_pm_mmap + (6*1024*1024*1024UL);
  uint64_t page_num_chunks = static_cast<uint64_t>( (srv_nvdimm_buf_pool_size)/4096);

  fprintf(stderr, "[DEBUG] log check begin !!!!\n");

  ulint offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;
    while (true) {
      PMEM_MMAP_MTRLOG_HDR mtr_recv_hdr;
      memcpy(&mtr_recv_hdr, gb_pm_mmap+offset, sizeof(PMEM_MMAP_MTRLOG_HDR));
      if (mtr_recv_hdr.len == 0) break;

      fprintf(stderr, "[DEBUG] %lu:%lu log here! %lu flag: %d\n"
          , mtr_recv_hdr.space, mtr_recv_hdr.page_no, offset, mtr_recv_hdr.need_recv);

      pmem_nc_log_map[std::make_pair(mtr_recv_hdr.space, mtr_recv_hdr.page_no)].push_back(offset);

      // add
      pmem_nc_log_check[std::make_pair(mtr_recv_hdr.space, mtr_recv_hdr.page_no)] = false;

      // (debug): we flush all pages with (need_recv==1)
      /*
      if (mtr_recv_hdr.need_recv == 1) {
        
        fil_space_t* space_t = fil_space_get(mtr_recv_hdr.space);
        const page_id_t page_id(mtr_recv_hdr.space, mtr_recv_hdr.page_no);
        const page_size_t page_size(space_t->flags);

        if (buf_page_peek(page_id) == true) {
          fprintf(stderr, "[DEBUG] (%lu:%lu) already exists in BUFFER!\n", mtr_recv_hdr.space, mtr_recv_hdr.page_no);
        }

        
        void *p;
        posix_memalign(&p, 4096, 4096);
        memcpy(p, gb_pm_mmap + offset + sizeof(PMEM_MMAP_MTRLOG_HDR), 4096); 
        IORequest write_request(IORequest::WRITE);
        write_request.disable_compression();
        int check = fil_io(write_request, false, page_id, page_size, 0
            ,4096, p , NULL);
        fprintf(stderr, "!!! NC LOG  !!! fil_io check! %d\n", check);
        free(p);
        

        // buf_page_get_gen
        
        mtr_t tmp_mtr;
        mtr_start(&tmp_mtr);
        buf_block_t* tmp = buf_page_get_gen(
            page_id, page_size, RW_NO_LATCH, NULL, BUF_GET, __FILE__, __LINE__, &tmp_mtr);
        mtr_commit(&tmp_mtr);

        if (buf_page_is_corrupted(true, gb_pm_mmap+offset+sizeof(PMEM_MMAP_MTRLOG_HDR), page_size, false)) {
          fprintf(stderr, "[DEBUG] (%u:%u) page is corrupted!\n", mtr_recv_hdr.space, mtr_recv_hdr.page_no);
        } else {
          fprintf(stderr, "[DEBUG] (%u:%u) page is safe!\n", mtr_recv_hdr.space, mtr_recv_hdr.page_no);
        }

        fprintf(stderr, "[DEBUG] (%u:%u) we get from buffer offset:% lu\n"
            ,mtr_recv_hdr.space, mtr_recv_hdr.page_no, offset 
            );

        memcpy(tmp->frame, gb_pm_mmap + offset + sizeof(PMEM_MMAP_MTRLOG_HDR), 4096);
      }
      */

      offset += sizeof(PMEM_MMAP_MTRLOG_HDR);
      offset += mtr_recv_hdr.len;

    }

  fprintf(stderr, "[DEBUG] log check end !!!\n");

  std::map<std::pair<uint64_t,uint64_t>, std::vector<uint64_t> >::iterator nclog_iter;

  for (uint64_t i=0 ; i< page_num_chunks; ++i) {

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

    // recovery from NC UNDO LOG 
    nclog_iter = pmem_nc_log_map.find(std::make_pair(space, page_no));
    if (nclog_iter != pmem_nc_log_map.end()) {
      PMEM_MMAP_MTRLOG_HDR mtr_recv_hdr;
      std::vector<uint64_t> nc_offset_vec = (*nclog_iter).second;
      for (uint64_t j=0; j<nc_offset_vec.size(); ++j) {
        uint64_t nc_offset = nc_offset_vec[j];
        memcpy(&mtr_recv_hdr, gb_pm_mmap+nc_offset, sizeof(PMEM_MMAP_MTRLOG_HDR));

        // double check
        uint64_t ck_space = mach_read_from_4( 
            gb_pm_mmap + nc_offset 
            + sizeof(PMEM_MMAP_MTRLOG_HDR) + FIL_PAGE_SPACE_ID);
        uint64_t ck_page_no = mach_read_from_4( 
            gb_pm_mmap + nc_offset 
            + sizeof(PMEM_MMAP_MTRLOG_HDR) + FIL_PAGE_OFFSET);
 
          fprintf(stderr, "[DEBUG] ck_space: %lu ck_page_no: %lu type: %d\n"
              , ck_space, ck_page_no, mtr_recv_hdr.type);

  
       if (buf_page_is_corrupted(true
             , gb_pm_mmap + nc_offset + sizeof(PMEM_MMAP_MTRLOG_HDR)
             , page_size, fsp_is_checksum_disabled(space))) {
          fprintf(stderr, "(%lu:%lu) page is corruptted!\n", space, page_no);
       } else {
          fprintf(stderr, "(%lu:%lu) page is good!\n", space, page_no);
       }
      } // end-for
    } // end-if 

    } else {
      fprintf(stderr, "(%lu:%lu) page is good!\n", space, page_no);
      pmem_nc_buffer_map[std::make_pair(space,page_no)].push_back(i*4096);
    }

   }
}

void debug_func() {
  fprintf(stderr, "[DEBUG] !!!!\n");
}

/* nc logging */

void nc_recv_analysis() {
 uint64_t space, page_no;
 unsigned char *addr = gb_pm_mmap + (1*1024*1024*1024UL);
 uint64_t page_num_chunks = static_cast<uint64_t>( (8*147324928UL)/4096);

 uint64_t lsn = 0;
 uint64_t oldest_lsn = 0;

 fprintf(stderr, "[DEBUG] NVDIMM Caching page analysis begin! total pages v2: %lu\n", page_num_chunks);

 for (uint64_t i=0; i < page_num_chunks; ++i) {
 //for (uint64_t i=0; i < srv_nvdimm_buf_pool_size; i+= UNIV_PAGE_SIZE) {

  space = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.space();
  page_no = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.page_no();
  unsigned char *frame = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->frame;

  // HOT DEBUG //
  //space = reinterpret_cast<buf_block_t*>((addr+ i ))->page.id.space();
  //page_no = reinterpret_cast<buf_block_t*>((addr+ i ))->page.id.page_no();
  //unsigned char *frame = (unsigned char*)(addr+ i);

  if (space != 27 && space != 29) {
    fprintf(stderr, "[DEBUG] we miss the pages %lu:%lu\n", space, page_no);
    if (space == 4294967295 
         && page_no == 4294967295) {
      continue;
    } else {
      break;
    }
  } else {
    fprintf(stderr, "[DEBUG] we get this page %lu:%lu\n", space, page_no);
  }

  // check 
  if (space != mach_read_from_4(frame + FIL_PAGE_SPACE_ID)
      || page_no != mach_read_from_4(frame + FIL_PAGE_OFFSET)) {
    fprintf(stderr, "[DEBUG] wrong frame info!\n (%lu:%lu) (%lu:%lu)", space, page_no
        , mach_read_from_4(frame + FIL_PAGE_SPACE_ID)
        , mach_read_from_4(frame + FIL_PAGE_OFFSET));
  }

  lsn =  mach_read_from_8(frame + FIL_PAGE_LSN);
  if (!oldest_lsn || oldest_lsn > lsn ) {
    oldest_lsn = lsn;
  }

#ifdef PMEM_RECV_DEBUG  
  fil_space_t* space_t = fil_space_get(space);
  const page_id_t page_id(space,page_no);
  const page_size_t page_size(space_t->flags);
  if (buf_page_is_corrupted(true, frame, page_size,
        fsp_is_checksum_disabled(space))) {
    fprintf(stderr, "(%lu:%lu) page is corruptted! lsn: %lu\n", space, page_no, mach_read_from_8(frame + FIL_PAGE_LSN));
  } else {
    fprintf(stderr, "(%lu:%lu) page is good! lsn: %lu\n", space, page_no, mach_read_from_8(frame + FIL_PAGE_LSN));
  }
#endif

  // we store relative position of nc page
  pmem_nc_buffer_map[std::make_pair(space,page_no)].push_back(i*sizeof(buf_block_t));
  nc_oldest_lsn = oldest_lsn;
  ib::info() << "oldest_lsn in NC pages: " << oldest_lsn;

 }
}

uint64_t pm_mmap_recv_check_nc_buf(uint64_t space, uint64_t page_no) {
  std::map<std::pair<uint64_t,uint64_t>, std::vector<uint64_t> >::iterator ncbuf_iter;
  ncbuf_iter = pmem_nc_buffer_map.find(std::make_pair(space,page_no));
  if (ncbuf_iter != pmem_nc_buffer_map.end()) { 
    std::vector<uint64_t> nc_offset_vec = (*ncbuf_iter).second;
    uint64_t nc_offset;
    for (uint64_t i=0; i<nc_offset_vec.size(); i++) {
      nc_offset = nc_offset_vec[i];
      unsigned char *nc_frame = reinterpret_cast<buf_block_t*>
        ((gb_pm_mmap + (1*1024*1024*1024UL) + nc_offset))->frame;

      fprintf(stderr, "[DEBUG] NC BUF (%lu:%lu) offset: %lu page_lsn: %lu i: %lu vec:size: %d\n", 
          space, page_no, nc_offset
          , mach_read_from_8(nc_frame + FIL_PAGE_LSN)
          , i, nc_offset_vec.size());
      if (space != mach_read_from_4(nc_frame + FIL_PAGE_SPACE_ID)
          || page_no != mach_read_from_4(nc_frame + FIL_PAGE_OFFSET)) {
        fprintf(stderr, "[DEBUG] wrong buffer page info! %u:%u\n", space, page_no);
      }     
    }
    return nc_offset;
  } else {
    return -1;
  }
}


