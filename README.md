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

## Sample

redis storage engine connects local-redis(127.0.0.1:6379) which is hard-coded (LOL).
So, you need to install redis to use this.

```sql
Enter password:
Welcome to the MySQL monitor.  Commands end with ; or \g.
Your MySQL connection id is 8
Server version: 8.0.20-debug Source distribution

Copyright (c) 2000, 2020, Oracle and/or its affiliates. All rights reserved.

Oracle is a registered trademark of Oracle Corporation and/or its
affiliates. Other names may be trademarks of their respective
owners.

Type 'help;' or '\h' for help. Type '\c' to clear the current input statement.

mysql> install plugin redis soname 'ha_redis.so';
Query OK, 0 rows affected (0.01 sec)

mysql> show engines;
+--------------------+---------+----------------------------------------------------------------+--------------+------+------------+
| Engine             | Support | Comment                                                        | Transactions | XA   | Savepoints |
+--------------------+---------+----------------------------------------------------------------+--------------+------+------------+
| REDIS              | YES     | Redis storage engine                                           | NO           | NO   | NO         |
| MEMORY             | YES     | Hash based, stored in memory, useful for temporary tables      | NO           | NO   | NO         |
| InnoDB             | DEFAULT | Supports transactions, row-level locking, and foreign keys     | YES          | YES  | YES        |
| PERFORMANCE_SCHEMA | YES     | Performance Schema                                             | NO           | NO   | NO         |
| MyISAM             | YES     | MyISAM storage engine                                          | NO           | NO   | NO         |
| FEDERATED          | NO      | Federated MySQL storage engine                                 | NULL         | NULL | NULL       |
| MRG_MYISAM         | YES     | Collection of identical MyISAM tables                          | NO           | NO   | NO         |
| BLACKHOLE          | YES     | /dev/null storage engine (anything you write to it disappears) | NO           | NO   | NO         |
| CSV                | YES     | CSV storage engine                                             | NO           | NO   | NO         |
| ARCHIVE            | YES     | Archive storage engine                                         | NO           | NO   | NO         |
+--------------------+---------+----------------------------------------------------------------+--------------+------+------------+
10 rows in set (0.00 sec)

mysql> create database redis_db;
Query OK, 1 row affected (0.02 sec)

mysql> use redis_db
Database changed


mysql> CREATE TABLE r1 (
    -> id INT NOT NULL,
    -> c1 VARCHAR(50)
    -> ) ENGINE = redis;
Query OK, 0 rows affected (0.02 sec)

mysql> show create table r1\G
*************************** 1. row ***************************
       Table: r1
Create Table: CREATE TABLE `r1` (
  `id` int NOT NULL,
  `c1` varchar(50) DEFAULT NULL
) ENGINE=REDIS DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci
1 row in set (0.01 sec)

mysql> insert into r1(id, c1) values (0, "need to set SBL");
ERROR 1662 (HY000): Cannot execute statement: impossible to write to binary log since BINLOG_FORMAT = ROW and at least one table uses a storage engine limited to statement-based logging.

mysql> set session binlog_format = statement;
Query OK, 0 rows affected (0.00 sec)

mysql> insert into r1(id, c1) values (1, "Hello_redis_storage_engine!!");
Query OK, 1 row affected (0.00 sec)

mysql> insert into r1(id, c1) values (2, "Yeeey!!");
Query OK, 1 row affected (0.00 sec)

mysql> select * from r1;
+----+------------------------------+
| id | c1                           |
+----+------------------------------+
|  1 | Hello_redis_storage_engine!! |
|  2 | Yeeey!!                      |
+----+------------------------------+
2 rows in set (0.00 sec)
```

### redis side

```sh
127.0.0.1:6379> keys *
1) "r1"
2) "test1"
127.0.0.1:6379> lrange r1 0 -1
1) "1,Hello_redis_storage_engine!!,"
2) "2,Yeeey!!,"
```




## How to Test

I use MySQL Test Run(MTR), that is in `/test/redis_se`.
Please see that.

```sh
TBD
```

## LISENCE

GPL


