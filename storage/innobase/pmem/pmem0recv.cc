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

 // statisitics
 uint64_t safe_num=0, corrupt_num=0;

 for (uint64_t i=0; i < page_num_chunks; ++i) {
 //for (uint64_t i=0; i < srv_nvdimm_buf_pool_size; i+= UNIV_PAGE_SIZE) {

  space = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.space();
  page_no = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->page.id.page_no();
  unsigned char *frame = reinterpret_cast<buf_block_t*>((addr+ i * sizeof(buf_block_t)))->frame;

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
    ib::info() << "safe_num: " << safe_num << " courrpt_num: " 
        << corrupt_num << " total: " << (safe_num+corrupt_num);
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
