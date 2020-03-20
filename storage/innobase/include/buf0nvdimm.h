/** @file include/buf0nvdimm.h
  The database buffer pool for NVDIMM

  Created 3/20/2020 Mijin An
*******************************************************/

#ifndef buf0nvdimm_h
#define buf0nvdimm_h

#define NVDIMM_DEBUG_PRINT(fmt, args...) fprintf(stderr, "NVDIMM DEBUG: %s:%d:%s(): " fmt, \
        __FILE__, __LINE__, __func__, ##args)

#endif
