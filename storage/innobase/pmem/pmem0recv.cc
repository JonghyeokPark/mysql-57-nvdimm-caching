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

bool pm_mmap_recv(uint64_t start_offset, uint64_t end_offset) {

	mtr_t mtr;
	size_t tmp_offset = start_offset;

	while (true) {
		fprintf(stderr, "current tmp_offset: %lu:(%lu)\n", tmp_offset, end_offset);

		if (tmp_offset >= end_offset) {
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
      fprintf(stderr, "current log doesn't need to recvoery!\n");
      tmp_offset += (PMEM_MMAP_MTRLOG_HDR_SIZE + recv_mmap_hdr->len);
      free(recv_mmap_hdr);
      continue;
    }
  
		// page generation
		bool found;
		const page_size_t& page_size 
			= fil_space_get_page_size(recv_mmap_hdr->space, &found);
		
		if (!found) {
			fprintf(stderr, "This tablespace with that id (%lu) page (page_no: %lu) does not exist.\n"
			,	recv_mmap_hdr->space, recv_mmap_hdr->page_no);
		}	

		const page_id_t   page_id(recv_mmap_hdr->space, recv_mmap_hdr->page_no);
		if (buf_page_peek(page_id)) {
			buf_block_t*  block;
			mtr_start(&mtr);
			block = buf_page_get( page_id, page_size, RW_X_LATCH, &mtr);
			recv_recover_page(FALSE, block);
			fprintf(stderr, " page (page_no: %lu) is recovered!\n", recv_mmap_hdr->page_no);
			mtr_commit(&mtr); 	
		} else {
			// TODO(jhpark): implement nc version `recv_read_in_area()`
			fprintf(stderr, "current page doesn't exist buffer!\n");
			//ulint page_nos[1]; 
			//page_nos[0] = recv_mmap_hdr->page_no; // just one element
			//buf_read_recv_pages(TRUE, page_id.space(), page_nos, 1);
			//fprintf(stderr, "Recv pages at %lu\n", page_nos[0]); 
		  //recv_read_in_area(page_id);

      // Force recovery UNDO page 
      byte *mlog_data =  (byte*) malloc(recv_mmap_hdr->len);
      memcpy(mlog_data, gb_pm_mmap + tmp_offset + PMEM_MMAP_MTRLOG_HDR_SIZE, sizeof(*mlog_data)); 
      byte* ptr = mlog_data;
      byte* end_ptr = mlog_data + recv_mmap_hdr->len;
      mlog_id_t type;
      type = (mlog_id_t)((ulint)*ptr & ~MLOG_SINGLE_REC_FLAG);
    
      // get current page with space_id and page_no

			mtr_start(&mtr); 
			buf_block_t* block = buf_page_get( page_id, page_size, RW_X_LATCH, &mtr);
      byte* page = block->frame;
			if (page == NULL) {
				fprintf(stderr,"[JOGNQ] Tried to undo page but it is NULL!\n");
			} else {
				fprintf(stderr,"[JONGQ] YES!!! UNDO is right!!!\n");
			}
     	mtr_commit(&mtr); 

      switch(type) {
        case MLOG_UNDO_INSERT:
          ptr = trx_undo_parse_add_undo_rec(ptr, end_ptr, page);
					fprintf(stderr, "[JONGQ] success!\n");
					IORequest write_request(IORequest::WRITE);
					write_request.disable_compression(); // stil needed?
					fprintf(stderr, "[JONGQ] perform fil_io write!!!\n");
					int check = 0;
					check = fil_io(write_request, true, page_id, 
					univ_page_size, 0, univ_page_size.physical(), (void*) page, NULL);
					fprintf(stderr, "[JONGQ] fio_io result: %d\n", check);
      };
      // free mlog data
      free(mlog_data);

		}

    tmp_offset += (PMEM_MMAP_MTRLOG_HDR_SIZE + recv_mmap_hdr->len);
    free(recv_mmap_hdr);
	}

  return true;
}


void pm_mmap_recv_flush_buffer() {
	// step1. grap information of all nc buffer pages (space, page_no)
	// TODO(jhpark): need to modify to get pmem_log_buffer size automatically

	uint64_t cur_offset = 0;
	uint64_t total_buf_size = (1024*1024*1024*2UL);
	unsigned char* cur_gb_pm_buf = gb_pm_mmap + (1024*1024*3UL);

	while (true) {
		if (cur_offset >= total_buf_size) {
			break;
		}

		// In this phase, buffer pool is already allocated,
		// copy these frame to allocated frame of buf_block_t
		// buffer_pool_t -> buf_chunk_t -> buf_block_t -> frame
		// or---!!! just copy as bug_page_t (with UNIV_PAGE_SIZE)
		
		// TODO(jhpark): convert page size as constant variable
		byte* buf = (byte*) malloc(4096*1024); // 4KB page
		memcpy(buf, gb_pm_mmap + cur_offset, (4096*1024));
		// align page
		page_align(buf);

		// check page information
		// refer to btr0btr.ic
		// first check page_no and space_id
		
		unsigned long space_id = mach_read_from_4(buf + FIL_PAGE_SPACE_ID);
		unsigned long page_no = mach_read_from_4(buf + FIL_PAGE_OFFSET);  			
		
		fprintf(stderr, "[JONGQ] cur_offset: %lu, space_id: %lu, page_no: %lu\n"
		,cur_offset, space_id, page_no);		

		if (space_id == 28 || space_id == 30) {
			//&& page_no == 0)) {
			// perform fil_io
			IORequest write_request(IORequest::WRITE);
			write_request.disable_compression(); // stil needed?

			// similar process, partila updates! 
			write_request.dblwr_recover();
			fprintf(stderr, "[JONGQ] perform fil_io write!!!\n");
			int check = 0;
			check = fil_io(write_request, true, page_id_t(space_id, page_no), 
					univ_page_size, 0 ,univ_page_size.physical(), (void*) buf, NULL);

			fprintf(stderr, "[JONGQ] fil_io check: %d!\n", check);
		}

		cur_offset += (4096*1024);
		free(buf);
	}

    // step2. call fil_io() function to flush current 
    // note that changes on these pages are not atomic 
    // they might have partial updates
}

/* nc-logging */
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

void nc_recv_analysis() {
 uint64_t space, page_no;
 unsigned char *addr = gb_pm_mmap + (1*1024*1024*1024UL);
 uint64_t page_num_chunks = static_cast<uint64_t>( (8*147324928UL)/4096);

 nc_mtrlog_analysis();

 // statisitics
 uint64_t safe_num=0, corrupt_num=0;

 for (uint64_t i=0; i < page_num_chunks; ++i) {
 //for (uint64_t i=0; i < srv_nvdimm_buf_pool_size; i+= UNIV_PAGE_SIZE) {

  space = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.space();
  page_no = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.page_no();
  unsigned char *frame = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->frame;

  // HOT DEBUG //
  //space = reinterpret_cast<buf_block_t*>((addr+ i ))->page.id.space();
  //page_no = reinterpret_cast<buf_block_t*>((addr+ i ))->page.id.page_no();
  //unsigned char *frame = (unsigned char*)(addr+ i);

  if (space != 28 && space != 30 && space != 32) {
    if (space == 4294967295
         && page_no == 4294967295) {
      continue;
    } else {
//      continue;
      break;
    }
  } else {
#ifdef UNIV_DEBUG
    ib::info() << "obtaine NC page: " << space << ":" << page_no;
    // check
    if (space != mach_read_from_4(frame + FIL_PAGE_SPACE_ID)
      || page_no != mach_read_from_4(frame + FIL_PAGE_OFFSET)) {
      ib::info() << " wrong NC page frame info expected: "
        << space << ":" << page_no
        << " current value: " << mach_read_from_4(frame + FIL_PAGE_SPACE_ID)
        << ":" << mach_read_from_4(frame + FIL_PAGE_OFFSET);
    }
#endif

    // check corruption
    //mtr_t tmp_mtr;
    //mtr_start(&tmp_mtr);

    unsigned long check;
    fseg_header_t* seg_header = frame + PAGE_HEADER + PAGE_BTR_SEG_LEAF;
    check = mach_read_from_4(seg_header + FSEG_HDR_SPACE);
    //fprintf(stderr,"[DEBUG] frame : %p \n", frame);
    if (check == 1) {
      corrupt_num++;
    } else {
      safe_num++;
    }

    // we store relative position of nc page
    pmem_nc_buffer_map[std::make_pair(space,page_no)].push_back(i*sizeof(buf_block_t));
    ib::info() << "safe_num: " << safe_num << " courrpt_num: " << corrupt_num << " total: " << (safe_num+corrupt_num);

    //tmp_mtr.discard_modifications();
    //mtr_commit(&tmp_mtr);
  }

#ifdef PMEM_RECV_DEBUG
  fil_space_t* space_t = fil_space_get(space);
  const page_id_t page_id(space,page_no);
  const page_size_t page_size(space_t->flags);
  if (buf_page_is_corrupted(true, frame, page_size,
        fsp_is_checksum_disabled(space))) {
    corrupt_num++;
    fprintf(stderr, "(%lu:%lu) page is corruptted! lsn: %lu\n", space, page_no, mach_read_from_8(frame + FIL_PAGE_LSN));
  } else {
    safe_num++;
    fprintf(stderr, "(%lu:%lu) page is good! lsn: %lu\n", space, page_no, mach_read_from_8(frame + FIL_PAGE_LSN));
  }
#endif

 }
}


int nc_mtrlog_recv_read_hdr(unsigned char* addr) {

  // read 4Byte
  int type = -1;
  memcpy(&type, addr, sizeof(int));
  if (type == 1) {
    ib::info() << "this is insert";
  } else if (type == 2) {
    ib::info() << "this is update";
  } else if (type == 3) {
    ib::info() << "this is delete";
  } else {
    ib::info() << "[debug] type: " << type;
  }

  return type;
}

void nc_mtrlog_analysis() {
  uint64_t space, page_no;
  unsigned char *addr = gb_pm_mmap + (1*1024*1024*1024UL) + (2*1024*1024*1024UL);

  uint64_t page_num_chunks = static_cast<uint64_t>( (8*147324928UL)/4096);
  unsigned long type;
  for (uint64_t i=0; i < page_num_chunks*2; ++i) {
    type = nc_mtrlog_recv_read_hdr(addr);
    if (type == 1) {

      PMEM_MTR_INSERT_LOGHDR mtrlog_insert;
      memcpy(&mtrlog_insert, addr, sizeof(PMEM_MTR_INSERT_LOGHDR));
      //addr += sizeof(PMEM_MTR_INSERT_LOGHDR);

      // read from
      ib::info() << "MTR INSERT RECORD"
        << mtrlog_insert.space << ":" << mtrlog_insert.page_no
        << " rec_offset: " << mtrlog_insert.rec_offset
        << " rec_size: " << mtrlog_insert.rec_size
        << " cur_rec_off: " << mtrlog_insert.cur_rec_off
        << " pd_off: " << mtrlog_insert.pd_offset
        << " pd_size: " << mtrlog_insert.pd_size
        << " valid: " << mtrlog_insert.valid;


    } else if (type == 2) {

      PMEM_MTR_UPDATE_LOGHDR mtrlog_update;
      memcpy(&mtrlog_update, addr, sizeof(PMEM_MTR_UPDATE_LOGHDR));
     //addr += sizeof(PMEM_MTR_UPDATE_LOGHDR);

      // read from
      ib::info() << mtrlog_update.space << ":" << mtrlog_update.page_no
        << " rec_offset: " << mtrlog_update.rec_off
        << " rec_size: " << mtrlog_update.rec_size
        << " valid: " << mtrlog_update.valid;


    } else if (type == 3) {

      PMEM_MTR_DELETE_LOGHDR mtrlog_delete;
      memcpy(&mtrlog_delete, addr, sizeof(PMEM_MTR_DELETE_LOGHDR));
     //addr += sizeof(PMEM_MTR_UPDATE_LOGHDR);

      // read from
      ib::info() << mtrlog_delete.space << ":" << mtrlog_delete.page_no
        << " rec_offset: " << mtrlog_delete.rec_off
        << " rec_size: " << mtrlog_delete.rec_size
        << " valid: " << mtrlog_delete.valid;


    }

    addr += 1024;
  }
}

