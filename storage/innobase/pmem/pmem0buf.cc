// Jong-Hyeok Park
#include "pmem_mmap_obj.h"
#include "ut0mutex.h"

// (jhpark): For now, we exploit current gb_pm_mmap region for NVDIMM Caching page.
// Total number of pool size is 8GB, 1MB is for undo mtr log region and 2GB is for NC buffer
// cur_offset starts after mtr_log region actual offset means (mtrlog retion size + cur_offset)

extern PMEM_MMAP_MTRLOG_BUF* mmap_mtrlogbuf;
extern unsigned char* gb_pm_mmap;
unsigned char* gb_pm_buf;

PMEM_MMAP_BUF_SYS* mmap_buf_sys = NULL;

void
pm_mmap_buf_init(const uint64_t size) {
	mmap_buf_sys = static_cast<PMEM_MMAP_BUF_SYS*>(malloc(sizeof (PMEM_MMAP_BUF_SYS)));

	pthread_mutex_init(&mmap_buf_sys->bufMutex, NULL);
	mmap_buf_sys->size = size;
	mmap_buf_sys->n_pages = 0;
	mmap_buf_sys->cur_offset = 0;
	
	gb_pm_buf = gb_pm_mmap + mmap_mtrlogbuf->size;
	PMEMMMAP_INFO_PRINT("pm_mmap_buf initialization finished! size: %lu\n", mmap_mtrlogbuf->size);
}

void 
pm_mmap_buf_free() {
	if (mmap_buf_sys != NULL) {
		ut_free(mmap_buf_sys);
		mmap_buf_sys = NULL;
	}
	pthread_mutex_destroy(&mmap_buf_sys->bufMutex);
}

/*
unsigned char*
pm_mmap_buf_chunk_alloc(unsigned long mem_size, ut_new_pfx_t* pfx) {
	// (jhpark): allocation already finished @pm_mmap_buf_init.
	// In this fucntion, we just pass the memory offset 
	size_t offset = mmap_buf_sys->cur_offset;
	mmap_buf_sys->cur_offset += mem_size;
	fprintf(stderr, "[JONGQ] chunk_alloc !!! mem_size: %lu, offset: %lu\n", mem_size, offset);
	pfx->m_size = mem_size;
	return gb_pm_buf + offset;	
}
*/

void
pm_mmap_buf_write(unsigned long len, void* buf) {
	size_t offset = mmap_buf_sys->cur_offset;
	memcpy_persist(gb_pm_buf + offset, buf, len);
	mmap_buf_sys->cur_offset += len;
	//fprintf(stderr, "[JONGQ] pm_mmap_buf_write finished! len: %lu\n", len);
}


