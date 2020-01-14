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

### Install hiredis
Please see https://github.com/redis/hiredis

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

# (Build mysql-server as you like...)

## Set result shared object
BUILD_DIR=/path/to/build_dir
cp ${BUILD_DIR}/plugin_output_directory/ha_redis.so ${BUILD_DIR}/lib/plugin/

# (Start mysqld)

mysql> INSTALL PLUGIN redis SONAME 'ha_redis.so';
```

Then, You can create a redis-storage-engine table!!

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


