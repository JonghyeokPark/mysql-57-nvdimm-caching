# NVDIMM Caching for MySQL 5.7

Optimize MySQL/InnoDB using NVDIMM 

## Build and install

1. Clone the source code: 

```bash
$ git clone https://github.com/meeeejin/mysql-57-nvdimm-caching.git
```

2. Change the value of `BASE_DIR` in the `build.sh` file to the desired value:

```bash
$ vi build.sh
#!/bin/bash

BASE_DIR=/home/xxx/mysql-57-nvdimm-caching
...
```

3. Run the script file:

```bash
$ ./build.sh
```

## Run

1. Add the following three server variables to the `my.cnf` file:

| System Variable                     | Description | 
| :---------------------------------- | :---------- |
| innodb_use_nvdimm_buffer            | Specifies whether to use NVDIMM cache. **true** or **false**. |
| innodb_nvdimm_buffer_pool_size      | The size in bytes of the NVDIMM cache. The default value is 2GB. |
| innodb_nvdimm_buffer_pool_instances | The number of regions that the NVDIMM cache is divided into. The default value is 1. |

For example:

```bash
$ vi my.cnf
...
innodb_use_nvdimm_buffer=true
innodb_nvdimm_buffer_pool_size=2G
innodb_nvdimm_buffer_pool_instances=1
...
```

2. Run the MySQL server:

```bash
$ ./bin/mysqld --defaults-file=my-nvdimm.cnf
``` 
