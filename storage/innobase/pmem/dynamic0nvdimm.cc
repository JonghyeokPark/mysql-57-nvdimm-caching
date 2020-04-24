#include "dynamic0nvdimm.h"

#include <iostream>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>

#include "fil0fil.h"    /* for fil_space_get_id_by_name() */
#include "srv0srv.h"    /* for srv_data_home, srv_undo_tablespaces */
//#include "fil0types.h"  /* for FIL_SPACE_ID */
#include "fsp0fsp.h"    /* for FSP_SPACE_FLAGS, FSP_HEADER_OFFSET */

#include <sstream> /* to_string() */

//#include "dict0dd.h"
//#include "dict0load.h"
//#include "dict0dict.h"
//#include "dict0priv.h"

std::vector<std::string> dynamic_nc_tables;
std::vector<unsigned int> dynamic_nc_space_ids;
//std::vector<unsigned int> dynamic_nc_undo_space_ids;

void save_dnc_tables (char* table_list) {
  if (table_list == NULL && *table_list ==0) {
    return;
  }

  std::string table_lists((const char*)table_list);
  std::string tokens;
  std::stringstream ss(table_lists);

  while (getline(ss, tokens, ';')) {
    dynamic_nc_tables.push_back(tokens);
  }
}

// TODO(jhpark): merge `svae_dnc_tables()` into this functions
void save_dnc_space_ids() {
  uint32 i = 0, space_id = 0;
  char buf[sizeof(uint32_t)];

  // sacn tpcc2000 directories

  for (i = 0; i < dynamic_nc_tables.size(); i++) {
    //ALWAYS_ASSERT(space_id != SPACE_UNKNOWN);

////////////////////////////////////////////////////////////////////
		std::string filepath = srv_data_home;
		filepath.append(dynamic_nc_tables[i].c_str());
		filepath.append(".ibd");

    byte*   tmp_buf;
    tmp_buf = static_cast<byte*>(ut_malloc_nokey(2 * UNIV_PAGE_SIZE));
		byte* page = static_cast<byte*>(ut_align(tmp_buf, UNIV_PAGE_SIZE));
		ut_ad(page == page_align(page));

    FILE* ifs;
    ifs = fopen(filepath.c_str(), "r");
		if (!ifs) {
			std::cerr << "Unable to open " << filepath << std::endl;
			return;
		}

    fread(page, UNIV_PAGE_SIZE, 1, ifs);

    space_id = fsp_header_get_space_id(page);
		dynamic_nc_space_ids.push_back(space_id);
    ut_free(tmp_buf);
////////////////////////////////////////////////////////////////////

//    std::string filepath = Fil_path::get_real_path(srv_data_home);
//    filepath.append(dynamic_nc_tables[i].c_str());
//    filepath.append(".ibd");

//    std::ifstream ifs(filepath, std::ios::binary);
//    if (!ifs) {
//      std::cerr << "Unable to open '" << filepath  << "'" << std::endl;
//      return;
//    }

    // Check the page size of the tablespace 
    // For (if any) compressed tablespace 
//    ifs.seekg(FSP_HEADER_OFFSET + FSP_SPACE_FLAGS, ifs.beg);
//    if ((ifs.rdstate() & std::ifstream::eofbit) != 0 ||
//        (ifs.rdstate() & std::ifstream::failbit) != 0 ||
//        (ifs.rdstate() & std::ifstream::badbit) != 0) {
//        return;
//    }    
//    ifs.seekg(FIL_PAGE_SPACE_ID, ifs.beg);
//    ifs.read(buf, sizeof(buf));

//    if (!ifs.good() || (size_t)ifs.gcount() < sizeof(buf)) {
//      std::cerr << "read tablespace id error\n";
//      return;
//    }
//    space_id = mach_read_from_4(reinterpret_cast<byte *>(buf));
//    dynamic_nc_space_ids.push_back(space_id);
//    ifs.close();
  }

  // add undo tablespace id
  // undo_space_name must be = "innodb_undo_001" 
  for (uint32 i = 1; i <= srv_undo_tablespaces; i++) {
    std::string undo_filename("innodb_undo_");
    std::string suffix("00");
    //////////////////////////////
    std::stringstream gstream;
    gstream << i;
    std::string g=gstream.str();
    suffix.append(g);
    //////////////////////////////
    undo_filename.append(suffix);

    space_id = fil_space_get_id_by_name(undo_filename.c_str());
    //ALWAYS_ASSERT(space_id != SPACE_UNKNOWN);
    dynamic_nc_space_ids.push_back(space_id);
    //dynamic_nc_undo_space_ids.push_back(space_id);
  }
}

void show_dnc_status() {
  
  unsigned int i = 0;
  std::cerr << "-- NVDIMM DYNAMIC CACHING STATUS:  " << std::endl;
  for (i = 0; i < dynamic_nc_tables.size(); i++) {
    std::cerr << "\t"
              << dynamic_nc_tables[i] << " : "
              << "space_id ( " << dynamic_nc_space_ids[i] << " )" << std::endl;
  }

  std::cerr << "[INFO] total # of undo_logs: " << srv_undo_tablespaces << std::endl;
  for (uint32 j = i; j <= i+srv_undo_tablespaces; j++) {
    std::cerr << "\t"
              << "UNDO table space : "
              << "space_id ( " << dynamic_nc_space_ids[j] << " ) " << std::endl;
  }
}
