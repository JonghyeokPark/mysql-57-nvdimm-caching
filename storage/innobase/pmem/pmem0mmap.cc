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
unsigned char* gb_pm_mtrlog;

int gb_pm_mmap_fd;

// recovery
bool is_pmem_recv = false;
uint64_t pmem_recv_offset = 0;
uint64_t pmem_recv_size = 0;
/* nc-logging */
std::map<std::pair<uint64_t,uint64_t> ,std::vector<uint64_t> > pmem_nc_buffer_map;
std::map<std::pair<uint64_t,uint64_t> , std::vector<uint64_t> > pmem_nc_page_map;
// YYY
std::map<uint64_t, uint64_t> pmem_mtrlog_offset_map;
std::map<std::pair<uint64_t,uint64_t> , uint64_t > pmem_nc_shadow_map;

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
	
	  // TODO(jhpark): real recovery process
		is_pmem_recv = true;
  }

  // Force to set NVIMMM
  setenv("PMEM_IS_PMEM_FORCE", "1", 1);
  PMEMMMAP_INFO_PRINT("Current kernel does not recognize NVDIMM as the persistenct memory \
      We force to set the environment variable PMEM_IS_PMEM_FORCE \
      We call mync() instead of mfense()\n");

  return gb_pm_mmap;
}

void pm_mmap_free(const uint64_t pool_size) {
	// free mtrMutex 
	munmap(gb_pm_mmap, pool_size);
	close(gb_pm_mmap_fd);
	PMEMMMAP_INFO_PRINT("munmap persistent memroy region\n");
}


