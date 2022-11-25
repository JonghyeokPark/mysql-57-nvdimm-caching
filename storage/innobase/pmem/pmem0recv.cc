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
      unsigned char *nc_frame = 
        ((gb_pm_mmap + (6*1024*1024*1024UL) + 13107200 + nc_offset));

        if (space != mach_read_from_4(nc_frame + FIL_PAGE_SPACE_ID)
          || page_no != mach_read_from_4(nc_frame + FIL_PAGE_OFFSET)) {
        fprintf(stderr, "[DEBUG] wrong buffer page info! %u:%u but we found %u:%u\n"
            , space, page_no
            , mach_read_from_4(nc_frame + FIL_PAGE_SPACE_ID)
            , mach_read_from_4(nc_frame + FIL_PAGE_OFFSET));
      }
    }
    return nc_offset;
  } else {
    return -1;
  }
}

void nc_recv_analysis() {
 uint64_t space, page_no;
 unsigned char *addr = gb_pm_mmap + (6*1024*1024*1024UL);
 uint64_t page_num_chunks = static_cast<uint64_t>( (8*147324928UL)/4096);

 struct timeval start, end;
 gettimeofday(&start, NULL);

 // statisitics
 uint64_t safe_num=0, corrupt_num=0;

 for (uint64_t i=0; i < page_num_chunks; ++i) {

   unsigned char* frame = addr + 13107200 + (i * 4096);
   space = mach_read_from_4(frame + FIL_PAGE_SPACE_ID);
   page_no = mach_read_from_4(frame + FIL_PAGE_OFFSET);
 
  if (! (space >= 24 && space <= 32) ) {

    if (space == 4294967295
         && page_no == 4294967295) {
      continue;
    } else {
      continue;
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

    /*
    unsigned long check;
    fseg_header_t* seg_header = frame + PAGE_HEADER + PAGE_BTR_SEG_LEAF;
    check = mach_read_from_4(seg_header + FSEG_HDR_SPACE);
    //fprintf(stderr,"[DEBUG] frame : %p \n", frame);
    if (check == 1) {
      corrupt_num++;
    } else {
      safe_num++;
    }
    */

    // debug
    if ( ! 
        (   mach_read_from_1(frame + PAGE_HEADER + PAGE_DIRECTION + 1) == 100
          || mach_read_from_1(frame + PAGE_HEADER + PAGE_DIRECTION + 1) == 200
        ) 
       ){
    
      //ib::info() << "in-update flag error! page: " 
      //  << space << " : " << page_no << " val: " 
      //  << mach_read_from_1(frame + PAGE_HEADER + PAGE_DIRECTION + 1);
    }

    bool corrupt_flag = false;
    if (mach_read_from_1(frame + PAGE_HEADER + PAGE_DIRECTION + 1) == 100) {
      corrupt_flag = true;
      corrupt_num++;
    } else if (mach_read_from_1(frame + PAGE_HEADER + PAGE_DIRECTION + 1) == 200) {
      corrupt_flag =false;
      safe_num++;
    } else {
      corrupt_flag = true;
      corrupt_num++;
    }

    // we store relative position of nc page
    pmem_nc_buffer_map[std::make_pair(space,page_no)].push_back(i*4096);

    uint64_t cur_page_lsn = mach_read_from_8(frame + FIL_PAGE_LSN);

    if (cur_page_lsn!=0
        //&& corrupt_flag 
        &&
        (!min_nc_page_lsn 
        || min_nc_page_lsn > cur_page_lsn)) { 
      min_nc_page_lsn = cur_page_lsn;
      ib::info() << "reset min_nc_page_lsn: " << min_nc_page_lsn;
    }

    ib::info() << "safe_num: " << safe_num << " courrpt_num: " 
        << corrupt_num << " total: " << (safe_num+corrupt_num)
        << " page: " << space << ":" << page_no;
  }
 }

 gettimeofday(&end, NULL);
 fprintf(stderr, "pmem_scan_time: %f seconds\n",
     (double) (end.tv_usec - start.tv_usec) / 1000000 +
     (double) (end.tv_sec - start.tv_sec));



}
