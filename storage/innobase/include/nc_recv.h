#ifndef __NC_RECV_H_
#define __NC_RECV_H_

#include "mtr0types.h"
#include "ut0new.h"
#include "ut0list.h"
#include "ut0mutex.h"

struct nc_recv_sys_t {
	ib_mutex_t mutex;
	ib_mutex_t    writer_mutex;

	byte* buf;
	uint64_t scanned_lsn;
	uint64_t recovered_lsn;
	uint64_t checkpoint_lsn;

	mem_heap_t* heap;
  hash_table_t* addr_hash;
	unsigned long n_addrs;
  uint64_t len;
  uint64_t recovered_offset;
};

extern nc_recv_sys_t* nc_recv_sys;

// tIPL logging & recovery
/** Block of log record data */
struct nc_recv_data_t{
  nc_recv_data_t*  next; /*!< pointer to the next block or NULL */
        /*!< the log record data is stored physically
        immediately after this struct, max amount
        RECV_DATA_BLOCK_SIZE bytes of it */
};

/** Stored log record struct */
struct nc_recv_t{
  mlog_id_t type; /*!< log record type */
  unsigned long   len;  /*!< log record body length in bytes */
  nc_recv_data_t*  data; /*!< chain of blocks containing the log record
        body */
  uint64_t   start_lsn;/*!< start lsn of the log segment written by
        the mtr which generated this log record: NOTE
        that this is not necessarily the start lsn of
        this log record */
  uint64_t   end_lsn;/*!< end lsn of the log segment written by
        the mtr which generated this log record: NOTE
        that this is not necessarily the end lsn of
        this log record */
  UT_LIST_NODE_T(nc_recv_t) rec_list;/*!< list of log records for this page */

};

struct nc_recv_addr_t {
	unsigned space:32;
	unsigned page_no:32;
	UT_LIST_BASE_NODE_T(nc_recv_t) rec_list;
	hash_node_t addr_hash;
};

void nc_recv_sys_create();
void nc_recv_sys_init(unsigned int available_memory);

#endif
