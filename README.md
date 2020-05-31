# redis storage engine

redis storage engine is a sample storage engine for learning MySQL more.
Starting from example storage engine, This engine is developed to use Redis for backend.
Its purpose is to understand MySQL deeper and just for fun!

I refered and learned many from example, csv, memory, tempTable, InnoDB storage engines.


## Supported queries

- DDL
  - [x] CREATE
  - [x] DROP
  - [x] TRUNCATE
- DML
  - [x] SELECT
  - [x] INSERT 
  - [x] DELETE
  - [x] UPDATE
  - [ ] REPLACE


## Prerequisite

- [Redis](https://github.com/antirez/redis) database
- [hiredis](https://github.com/redis/hiredis)


## How to Install

### Install MySQL from source

redis-storage-engine need MySQL source code to build with MySQL's cmake.

### Install hiredis

Please see the official repository https://github.com/redis/hiredis

(You need to be able to find hiredis with pkg-config command because CMake uses pkg-config)

```sh
$ pkg-config --list-all | grep hiredis
hiredis                        hiredis - Minimalistic C client library for Redis.
```

### Install redis storage engine

```sh
## Download repo
git clone https://github.com/tom--bo/redis-storage-engine
cd redis-storage-engine
cp -r redis /path/to/mysql-server/storage/

## (Build mysql-server as you like...)
# (Sample)
# cd /path/to/mysql-server
# mkdir bld; cd bld
# cmake .. -DDOWNLOAD_BOOST=1 -DWITH_BOOST=../myboost -DWITH_DEBUG=1 -DENABLE_DOWNLOADS=1 -DWITH_INNODB_EXTRA_DEBUG=1 -DCMAKE_C_FLAGS_RELWITHDEBINFO="-O0 -g" -DCMAKE_CXX_FLAGS_RELWITHDEBINFO="-O0 -g"
# make -j 16

## Set result shared object
BUILD_DIR=/path/to/build_dir
mkdir -p ${BUILD_DIR}/lib/plugin # if you don't have, create dir
cp ${BUILD_DIR}/plugin_output_directory/ha_redis.so ${BUILD_DIR}/lib/plugin/

## Set appropriate permission
# (sample) chown -R mysql:mysql ${BUILD_DIR}/plugin_output_directory/plugin

# (Start mysqld)

mysql> INSTALL PLUGIN redis SONAME 'ha_redis.so';
```

Then, You can create tables with redis-storage-engine!!

```sql
mysql> CREATE TABLE test(...) ENGINE = redis;
```



## How to Test

Before test, you need to install redis storage engine...


```sh
TBD
```

## LISENCE

GPL


