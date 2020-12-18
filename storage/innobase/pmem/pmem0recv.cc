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
std::map<std::pair<unsigned long, unsigned long>, uint64_t> nc_page_map;
std::set<unsigned long> nc_active_trx_ids;

/* Hello */

uint64_t pm_mmap_recv_check(PMEM_MMAP_MTRLOGFILE_HDR* log_fil_hdr) {
  // TODO(jhpark): add checkpoint process
  size_t tmp_offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;

	while (true) {
		fprintf(stderr, "current tmp_offset: %lu:(%lu)\n", tmp_offset, log_fil_hdr->size);
		if (tmp_offset >= log_fil_hdr->size) {
			break;
		}

		PMEM_LOG_HDR *recv_mmap_hdr = (PMEM_LOG_HDR *) malloc(PMEM_LOG_HDR_SZ);
		memcpy(recv_mmap_hdr, gb_pm_mmap + tmp_offset, PMEM_LOG_HDR_SZ);
		ut_ad(recv_mmap_hdr == NULL);

		fprintf(stderr, "[recovery] need_recv: %d len: %lu lsn: %lu space: %lu page_no: %lu\n"
										,recv_mmap_hdr->need_recv, recv_mmap_hdr->len, recv_mmap_hdr->lsn,
										recv_mmap_hdr->space, recv_mmap_hdr->page_no);

    if (recv_mmap_hdr->need_recv == 0) {
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

void pm_for_debug_REDO(uint64_t size) {
  //fprintf(stderr, "catch the debug point!\n");
  // looking thru whole NC REDO logs ...
  size_t tmp_offset = PMEM_MMAP_MTR_FIL_HDR_SIZE;
  while (true) {
    if (tmp_offset >= size) {
      break;
    }

    PMEM_LOG_HDR *recv_mmap_hdr = (PMEM_LOG_HDR *) malloc(PMEM_LOG_HDR_SZ);
    memcpy(recv_mmap_hdr, gb_pm_mmap + tmp_offset, PMEM_LOG_HDR_SZ);
    ut_ad(recv_mmap_hdr == NULL);

    fprintf(stderr, "=================================================================\n");
    fprintf(stderr, "[recovery] need_recv: %d len: %lu lsn: %lu space: %lu page_no: %lu\n"
										,recv_mmap_hdr->need_recv, recv_mmap_hdr->len, recv_mmap_hdr->lsn,
										recv_mmap_hdr->space, recv_mmap_hdr->page_no);
    fprintf(stderr, "[recovery] current tmp_offset: %lu\n", tmp_offset);
    fprintf(stderr, "=================================================================\n");

    tmp_offset += recv_mmap_hdr->len + PMEM_LOG_HDR_SZ;
    free(recv_mmap_hdr);
  }

  return;
}



///////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool pm_mmap_recv_nc_page_copy(unsigned long space_id, unsigned long page_no, void* buf) {
  std::map<std::pair<unsigned long, unsigned long>, uint64_t>::iterator iter;
	iter = nc_page_map.find(std::make_pair(space_id, page_no));

  unsigned char* cur_gb_pm_buf = gb_pm_mmap + (1024*1024*1024*1UL);
  byte* frame = (byte*) ut_align(cur_gb_pm_buf, UNIV_PAGE_SIZE);

  // validate the page
  bool found;
  const page_size_t page_size(fil_space_get_page_size(space_id, &found));
  assert(found);
  if (buf_page_is_corrupted(false, (const byte*) buf, page_size, fsp_is_checksum_disabled(space_id))) {
    fprintf(stderr, "[NC-RECOVERY] current NC page is corrupted!!!!\n"); 
    return false;
  }
  // copy the page
  memcpy(buf, (frame + iter->second), UNIV_PAGE_SIZE);
  //buf_page_print((const byte*) buf, page_size, BUF_PAGE_PRINT_NO_CRASH); 
  return true;
}

bool pm_mmap_recv_nc_page_validate(unsigned long space_id, unsigned long page_no) {
	std::map<std::pair<unsigned long, unsigned long>, uint64_t>::iterator iter;
	iter = nc_page_map.find(std::make_pair(space_id, page_no));
	return (iter != nc_page_map.end());
}

void pm_mmap_recv_add_active_trx_list(unsigned long trx_id) {
	// add all active trx_list do not allow duplication
	nc_active_trx_ids.insert(trx_id);
}

void pm_mmap_recv_show_trx_list() {
	// shwo all active transaction while conflict
	std::set<unsigned long>::iterator it;
	fprintf(stderr, " ============ active transaction ============== \n");
	for (it = nc_active_trx_ids.begin(); it != nc_active_trx_ids.end(); ++it) {
		fprintf(stderr, "%lu ", *it);
	}
	fprintf(stderr, "\n============================================= \n");
}



///////////// backup ////////////////
/*
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



*/


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

    /*
    if (recv_mmap_hdr->need_recv == false) {
      fprintf(stderr, "current log doesn't need to recvoery!\n");
      tmp_offset += (PMEM_MMAP_MTRLOG_HDR_SIZE + recv_mmap_hdr->len);
      free(recv_mmap_hdr);
      continue;
    }
    */
  
		// page generation
		bool found;
		const page_size_t& page_size 
			= fil_space_get_page_size(recv_mmap_hdr->space, &found);
		
		if (!found) {
			fprintf(stderr, "This tablespace with that id (%lu) page (page_no: %lu) does not exist.\n"
			,	recv_mmap_hdr->space, recv_mmap_hdr->page_no);
		}	

		const page_id_t   page_id(recv_mmap_hdr->space, recv_mmap_hdr->page_no);
    fprintf(stderr, "[MTR LOG] space : %lu page_no : %lu\n", recv_mmap_hdr->space
                                                           , recv_mmap_hdr->page_no);

    tmp_offset += (PMEM_MMAP_MTRLOG_HDR_SIZE + recv_mmap_hdr->len);
    free(recv_mmap_hdr);
	}

  return true;
}


void pm_mmap_recv_flush_buffer() {
	// step1. grap information of all nc buffer pages (space, page_no)
	// TODO(jhpark): need to modify to get pmem_log_buffer size automatically

	uint64_t cur_offset = 0, nc_pages = 0;
	uint64_t total_buf_size = (1024*1024*1024*4UL);
	unsigned char* cur_gb_pm_buf = gb_pm_mmap + (1024*1024*1024*1UL);

	// align page
	byte* frame = (byte*) ut_align(cur_gb_pm_buf, UNIV_PAGE_SIZE);
	while (true) {
		if (cur_offset >= total_buf_size) {
			break;
		}
	
		while (true) {
			if (cur_offset >= total_buf_size) break;
			byte* check = (byte*) malloc(2); // 2B checker 
			memcpy(check, frame+cur_offset, 2);
			
			// check whether index page
			if (mach_read_from_2(check) == FIL_PAGE_INDEX || 
            mach_read_from_2(check) == FIL_PAGE_INODE ||
            mach_read_from_2(check) == FIL_PAGE_COMPRESSED || 
            mach_read_from_2(check) == FIL_PAGE_TYPE_BLOB ||
            mach_read_from_2(check) == FIL_PAGE_TYPE_XDES ||
            mach_read_from_2(check) == FIL_PAGE_TYPE_FSP_HDR ||
            mach_read_from_2(check) == FIL_PAGE_TYPE_SYS 
            //|| mach_read_from_2(check) == FIL_PAGE_TYPE_ALLOCATED
            ) {
				byte* tmp = (byte*) malloc(UNIV_PAGE_SIZE);
				memcpy(tmp, frame + cur_offset - FIL_PAGE_TYPE, UNIV_PAGE_SIZE);
				uint64_t tmp_space = mach_read_from_4(tmp+FIL_PAGE_SPACE_ID);
				// detect NC pages
				if (tmp_space == 28 || tmp_space == 30 || tmp_space == 32 || tmp_space == 29) {
          cur_offset -= FIL_PAGE_TYPE;
					free(tmp);
					free(check);

					// add real NC pages into hash map
					byte* buf = (byte*) malloc(UNIV_PAGE_SIZE);
					memcpy(buf, frame + cur_offset, UNIV_PAGE_SIZE);

					unsigned long page_type = mach_read_from_2(buf + FIL_PAGE_TYPE);
					unsigned long space_id = mach_read_from_4(buf + FIL_PAGE_SPACE_ID);
					unsigned long page_no = mach_read_from_4(buf + FIL_PAGE_OFFSET);

					fprintf(stderr, "[JONGQ] cur_offset: %lu, page_type: %lu, space_id: %lu, page_no: %lu\n"
												,cur_offset, page_type, space_id, page_no);

          // store the start offset of nc pages
					nc_page_map[std::make_pair(space_id, page_no)] = cur_offset;
					nc_pages++;
					cur_offset += UNIV_PAGE_SIZE;
					free(buf);
					break;
				}
				free(tmp);
			} // check index page

			cur_offset += 2;
			free(check);
		}
	}

	fprintf(stderr, "[JONGQ] total NC pages: %lu\n", nc_pages);
	fprintf(stderr, "[JONGQ] ===================== NC page maps =========================== \n");
	std::map<std::pair<unsigned long, unsigned long>, uint64_t>::iterator iter;
	int count = 0;
	for (iter = nc_page_map.begin(); iter != nc_page_map.end(); iter++) {
		count++;
		fprintf(stderr, "KEY: (%lu, %lu) OFFSET: %d\n", iter->first.first, iter->first.second, iter->second);
	}
	fprintf(stderr, "[JONGQ] ============================================================= \n");

// step2 : call fil_io function to flush NC pages to disk
// Assume that, there is a original page having with same space_id and page_no for all nc pages 
// for (iter = nc_page_map.begin(); iter != nc_page_map.end(); iter++) {
// IORequest write_request(IORequest::WRITE);
// write_request.disable_compression();

//   byte* buf = (byte*) malloc(UNIV_PAGE_SIZE);
//   memcpy(buf, frame + iter->second, UNIV_PAGE_SIZE);
//   const page_id_t  page_id(iter->first.first, iter->first.second);   
//   int check = fil_io(write_request, true, 
//                        page_id, univ_page_size,
//                        0, univ_page_size.physical(), (void *) buf, NULL);

/*
  IORequest read_request(IORequest::READ);
  fil_space_t *space_xxx = fil_space_get(28);
  fil_space_open_if_needed(space_xxx);
  const page_id_t page_id3(28, 3063);
  ulint type = BUF_READ_ANY_PAGE;
  byte * buf2 = (byte*) malloc(UNIV_PAGE_SIZE);
  int check = fil_io(read_request, true, page_id3, univ_page_size, 0, univ_page_size.physical(), (void* )buf2, NULL);
  fprintf(stderr, "(read) fil_io check : %d\n", check);
  fprintf(stderr, "(read) content: %d\n", *buf2);

  IORequest write_request(IORequest::WRITE);
  write_request.disable_compression();
  write_request.disable_partial_io_warnings();
  byte* buf = (byte*) malloc(UNIV_PAGE_SIZE);
  memcpy(buf, buf2, UNIV_PAGE_SIZE);
  check = fil_io(write_request, true, 
                        page_id3, univ_page_size,
                        0, univ_page_size.physical(), (void *) buf, NULL);
  fprintf(stderr, "(write) fil_io check : %d\n", check);

  free(buf2);
  free(buf);
*/

}
