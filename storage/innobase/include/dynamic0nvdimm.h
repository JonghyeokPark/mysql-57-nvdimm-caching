/** @file include/dynamic0nvdimm.h
Data structures and methods for dynamic caching

Created 4/5/2020 Jong-Hyeok Park
*******************************************************/

#ifndef dynamic0nvdimm_h
#define dynamic0nvdimm_h

#include <string>
#include <vector>
#include <algorithm>  // std::find

#ifndef ALWAYS_ASSERT
#define ALWAYS_ASSERT(expr) (expr) ? (void)0 : abort()
#endif

const int NC_NO = 27;
const int NC_OL = 29;

extern std::vector<std::string> dynamic_nc_tables;
extern std::vector<unsigned int> dynamic_nc_space_ids;

/* Torkenize table_list and save each table names in 
 * `dynamic_nc_tables`
 */
void save_dnc_tables (char* table_list);

/* Get all space_ids from `dynmaic_nc_tables`
 * Last element MUST be the space id of the undo tablespace
 */
void save_dnc_space_ids ();

/* Print out all table names and space ids for dynamic cacching */
void show_dnc_status ();

/* Check whether this page is nvdimm caching page or not */
// TODO(jhpark): make this as macro !
inline bool check_nvdimm_caching_page(unsigned int space_id) {
  std::vector<unsigned int>::iterator iter;
  iter = std::find(dynamic_nc_space_ids.begin(), dynamic_nc_space_ids.end(), space_id);

  return (iter != dynamic_nc_space_ids.end());
}
#endif
