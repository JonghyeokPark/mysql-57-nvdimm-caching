#include "pmem_mmap_obj.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stddef.h>
#include "os0file.h"

PMEM_FILE* pf_init(const char* name) {
  PMEM_FILE* pf = (PMEM_FILE*) malloc(sizeof(PMEM_FILE));
  if (!pf) {
    goto err;
  }
  pf->name = (char*) malloc(PMEM_MAX_FILE_NAME_LENGTH);
  if (!pf->name) {
    goto err;
  }

  strcpy(pf->name, name);
  return pf;

err:
  fprintf(stderr, "[DEBUG] pf is NULL! %s\n", name);
  return NULL;
}

void pfc_free(PMEM_FILE_COLL* pfc) {
  assert(pfc);
  if (pfc->size > 0) {
    for (int i=0; i <pfc->size; ++i) {
      munmap(pfc->pfile[i]->addr, pfc->pfile[i]->len);
      free(pfc->pfile[i]);
    }
  }
  // free
  free(pfc->pfile);
  free(pfc);
}

PMEM_FILE_COLL* pfc_new(uint64_t file_size) {
   PMEM_FILE_COLL* pfc = (PMEM_FILE_COLL*) malloc(sizeof(PMEM_FILE_COLL));
   if (!pfc) {
     goto err;
   }
   pfc->pfile = (PMEM_FILE**) calloc(PMEM_MAX_FILES, sizeof(PMEM_FILE*));
   if(!pfc->pfile) {
    goto err;
   }
   
   pfc->size = 0;
   pfc->file_size = file_size;

   return pfc;
err:
   if (pfc) {
    pfc_free(pfc);
   }
   return NULL;
}

int pfc_find_by_name(PMEM_FILE_COLL* pfc, const char* name) {
  assert(pfc);
  for (int i=0; i<pfc->size; ++i) {
    if (strcmp(pfc->pfile[i]->name, name) == 0) {
      //fprintf(stderr, "[DEBUG] we find the file index:%d name:%s\n", i, name);
      return i;
    }
  }
  return -1;
}

int pfc_find_by_fd(PMEM_FILE_COLL* pfc, const int fd) {
  assert(pfc);
  assert(fd >0);
  for (int i=0; i<pfc->size; ++i) {
    if (pfc->pfile[i]->fd == fd) {
      //fprintf(stderr, "[DEBUG] we find the file index:%d fd:%d\n", i, fd);
      return i;
    }
  }
  return -1;
}

int pfc_append(PMEM_FILE_COLL* pfc, PMEM_FILE* pf) {
  assert(pfc);
  assert(pf);

  if (pfc->size >= PMEM_MAX_FILES) {
    fprintf(stderr, "[ERROR] # of pmem files exceeds thresholds(%d)\n", PMEM_MAX_FILES);
    return -1;
  }

  fprintf(stderr, "[DEBUG] pfc->size %d append pf !\n", pfc->size);
  pfc->pfile[pfc->size] = pf;
  pfc->size++;
  return 0;
}

int pfc_append_or_set(PMEM_FILE_COLL* pfc
    , unsigned long int create_mode , const char* name
    , const int fd, const size_t len) {

  int index = 0;
  PMEM_FILE *pf;

  // find pmem files
  if ( (index = pfc_find_by_name(pfc, name)) >= 0 ) {
    pfc->pfile[index]->fd = fd;
    pfc->pfile[index]->len = len;
    return 0;
  } else {
    pf = pf_init(name);

    if (create_mode == OS_FILE_OPEN 
        || create_mode ==  OS_FILE_OPEN_RAW
        || create_mode == OS_FILE_OPEN_RETRY) {
      // this file already existed but has not mmmaped
      int tmp_fd = open(name, O_RDWR | O_CREAT, 0777);
      if ((pf->addr = (char*) mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, tmp_fd, 0)) == NULL) {
        fprintf(stderr, "[ERROR] error mmap file! %s\n", name);
        return -1;
      }
    }
    else if (create_mode == OS_FILE_CREATE) {
      // this file has not existed and not mmaped
      int tmp_fd2 = open(name, O_RDWR | O_CREAT, 0777);
      char* addr =  (char *)mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, tmp_fd2, 0);
      if (addr == NULL) {
         fprintf(stderr, "[ERROR] error mmap file2! %s\n", name);
         return -1;
      }
      pf->addr =  addr;

//      if ( (pf->addr = (char*) mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, tmp_fd2, 0)) == NULL) {
//        fprintf(stderr, "[ERROR] error mmap file2! %s\n", name);
//        return -1;
//      }
    }
    pf->fd = fd;
  }
  return pfc_append(pfc, pf);
}

ssize_t pfc_pmem_io(PMEM_FILE_COLL* pfc,
    const int type, const int fd,
    void *buf, const uint64_t offset, unsigned long int n) {

  int index;
  ssize_t ret_bytes = 0;
  assert(pfc);

  if (pfc->size <= 0) {
    fprintf(stderr, "[ERROR] pmem file coll structure is empty!\n");
    return 0;
  }

  if ( (index = pfc_find_by_fd(pfc, fd)) >= 0) {
    if (type == PMEM_WRITE) {
      memcpy(pfc->pfile[index]->addr + offset, buf, (size_t) n);
      flush_cache(pfc->pfile[index]->addr + offset, n);
      ret_bytes = n;
    } else if (type == PMEM_READ) {
      memcpy(buf, pfc->pfile[index]->addr + offset, n);
      ret_bytes = n;
    }
    return ret_bytes;
  } else {
    fprintf(stderr, "[ERROR] no file with fd: %d todo IO\n", fd);
    return 0;
  }
}
