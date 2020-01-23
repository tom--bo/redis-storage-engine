/* Copyright (c) 2004, 2019, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redistoribute it and/or modify
  it under the terms of the GNU General Public License, version 2.0,
  as published by the Free Software Foundation.

  This program is also distributed with certain software (including
  but not limited to OpenSSL) that is licensed under separate terms,
  as designated in a particular file or component or in included license
  documentation.  The authors of MySQL hereby grant you an additional
  permission to link the program and your derivative works with the
  separately licensed software that they have included with MySQL.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License, version 2.0, for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA */

/**
  @file ha_redis.cc

  @brief
  The ha_redis engine is a sample storage engine for learning MySQL more.
  Starting from example storage engine, This engine uses Redis for backend.
  Its purpose is to understand MySQL deeper and just for fun!

  @details
  CREATE TABLE \<table name\> (...) ENGINE=REDIS;
*/

#include <sql/table.h>
#include "my_dbug.h"
#include "mysql/plugin.h"
#include "sql/sql_class.h"
#include "sql/sql_plugin.h"
#include "typelib.h"
#include "sql/field.h"

#include "ha_redis.h"
#include "hiredis.h" /* for redis */

static handler *redis_create_handler(handlerton *hton, TABLE_SHARE *table, bool partitioned, MEM_ROOT *mem_root);

handlerton *redis_hton;

/* Interface to mysqld, to check system tables supported by SE */
static bool redis_is_supported_system_table(const char *db,
                                              const char *table_name,
                                              bool is_sql_layer_system_table);

Redis_share::Redis_share() { thr_lock_init(&lock); }

static int redis_init_func(void *p) {
    redis_hton = (handlerton *)p;
    redis_hton->state = SHOW_OPTION_YES;
    redis_hton->create = redis_create_handler;
    redis_hton->flags = (
            HTON_ALTER_NOT_SUPPORTED| HTON_CAN_RECREATE | HTON_NO_PARTITION
    );
    redis_hton->is_supported_system_table = redis_is_supported_system_table;

    return 0;
}

/**
  @brief
  Redis of simple lock controls. The "share" it creates is a
  structure we will pass to each redis handler. Do you have to have
  one of these? Well, you have pieces that are used for locking, and
  they are needed to function.
*/
Redis_share *ha_redis::get_share() {
    Redis_share *tmp_share;
    DBUG_ENTER("ha_redis::get_share()");

    lock_shared_ha_data();
    if (!(tmp_share = static_cast<Redis_share *>(get_ha_share_ptr()))) {
        tmp_share = new Redis_share;
        if (!tmp_share) goto err;

        set_ha_share_ptr(static_cast<Handler_share *>(tmp_share));
    }
    err:
    unlock_shared_ha_data();
    DBUG_RETURN(tmp_share);
}

static handler *redis_create_handler(handlerton *hton, TABLE_SHARE *table, bool, MEM_ROOT *mem_root) {
    return new (mem_root) ha_redis(hton, table);
}

ha_redis::ha_redis(handlerton *hton, TABLE_SHARE *table_arg)
    : handler(hton, table_arg),
    current_position(0) {
}

/*
  List of all system tables specific to the SE.
  Array element would look like below,
     { "<database_name>", "<system table name>" },
  The last element MUST be,
     { (const char*)NULL, (const char*)NULL }

  This array is optional, so every SE need not implement it.
*/
static st_handler_tablename ha_redis_system_tables[] = {
        {(const char *)NULL, (const char *)NULL}
};

/**
  @brief Check if the given db.tablename is a system table for this SE.

  @param db                         Database name to check.
  @param table_name                 table name to check.
  @param is_sql_layer_system_table  if the supplied db.table_name is a SQL
                                    layer system table.

  @return
    @retval true   Given db.table_name is supported system table.
    @retval false  Given db.table_name is not a supported system table.
*/
static bool redis_is_supported_system_table(const char *db,
                                              const char *table_name,
                                              bool is_sql_layer_system_table) {
    st_handler_tablename *systab;

    if (is_sql_layer_system_table) return false; // Does this SE support "ALL" SQL layer system tables ?

    // Check if this is SE layer system tables
    systab = ha_redis_system_tables;
    while (systab && systab->db) {
        if (systab->db == db && strcmp(systab->tablename, table_name) == 0)
            return true;
        systab++;
    }

    return false;
}

/**
  @brief
  Used for opening tables. The name will be the name of the list key in redis.

  @see
  handler::ha_open() in handler.cc
*/
int ha_redis::open(const char *tname, int, uint, const dd::Table *) {
    DBUG_ENTER("ha_redis::open");

    if (!(share = get_share())) return 1;
    thr_lock_data_init(&share->lock, &lock, NULL);

    c = redisConnect("127.0.0.1", 6379);
    if (c != NULL && c->err) {
        DBUG_RETURN(-1);
    }

    share->table_name = get_table_name(tname);

    DBUG_RETURN(0);
}

/**
  @brief
  Closes a table.
*/
int ha_redis::close(void) {
    // DBUG_TRACE;
    return 0;
}

/**
  @brief
  write_row() inserts a row. No extra() hint is given currently if a bulk load
  is happening. buf() is a byte array of data. You can use the field
  information to extract the data from the native byte array type.

  @details
  Redis of this would be:
  @code
  for (Field **field=table->field ; *field ; field++)
  {
    ...
  }
  @endcode

  See ha_tina.cc for an example of extracting all of the data as strings.
*/
int ha_redis::write_row(uchar *) {
    DBUG_ENTER("ha_redis::write_row");
    char attr_buf[1024];
    std::string record_str = "";

    ha_statistic_increment(&System_status_var::ha_write_count);

    String attribute(attr_buf, sizeof(attr_buf), &my_charset_bin);
    my_bitmap_map *org_bitmap = tmp_use_all_columns(table, table->read_set);

    for(Field **field = table->field; *field; field++) {
        const char *p;
        const char *end;

        (*field)->val_str(&attribute, &attribute);
        p = attribute.ptr();
        end = attribute.length() + p;

        for(; p < end; p++) {
            record_str += *p;
        }
        record_str += ",";
    }
    record_str.pop_back();
    tmp_restore_column_map(table->read_set, org_bitmap);

    std::string cmd = "RPUSH " + share->table_name += " " + record_str;
    redisReply *ret = (redisReply *)redisCommand(c, cmd.c_str());
    if(ret) {
        freeReplyObject(ret);
    }

    stats.records++;
    DBUG_RETURN(0);
}

/**
  @brief
  Yes, update_row() does what you expect, it updates a row. old_data will have
  the previous row record in it, while new_data will have the newest data in it.
  Keep in mind that the server can do updates based on ordering if an ORDER BY
  clause was used. Consecutive ordering is not guaranteed.
*/
int ha_redis::update_row(const uchar *, uchar *) {
    DBUG_ENTER("ha_redis::write_row");
    ha_statistic_increment(&System_status_var::ha_update_count);
    char attr_buf[1024];
    std::string record_str = "";
    String attribute(attr_buf, sizeof(attr_buf), &my_charset_bin);
    my_bitmap_map *org_bitmap = tmp_use_all_columns(table, table->read_set);

    for(Field **field = table->field; *field; field++) {
        const char *p;
        const char *end;
        const bool was_null = (*field)->is_null();

        if (was_null) {
            (*field)->set_default();
            (*field)->set_notnull();
        }

        (*field)->val_str(&attribute, &attribute);

        if (was_null) (*field)->set_null();

        p = attribute.ptr();
        end = attribute.length() + p;

        for(; p < end; p++) {
            record_str += *p;
        }
        record_str += ",";
    }
    record_str.pop_back();
    tmp_restore_column_map(table->read_set, org_bitmap);

    std::string cmd = "LSET " + share->table_name += " " + std::to_string(current_position-1) + " " + record_str;
    redisReply *ret = (redisReply *)redisCommand(c, cmd.c_str());
    if(ret) {
        freeReplyObject(ret);
    }

    DBUG_RETURN(0);
}

/**
  @brief
  This will delete a row.
  This set the value ",". And After setting all deleting rows Actually delete them in rnd_end().
  This is temporal implimentation.
*/
int ha_redis::delete_row(const uchar *) {
    // (current_position-1)に","をセットする
    ha_statistic_increment(&System_status_var::ha_delete_count);
    std::string cmd = "LSET " + share->table_name += " " + std::to_string(current_position-1) + " ,";
    redisReply *ret = (redisReply *)redisCommand(c, cmd.c_str());
    if(ret) {
        freeReplyObject(ret);
    }

    return 0;
}

/**
  @brief
  Positions an index cursor to the index specified in the handle. Fetches the
  row if available. If the key value is null, begin at the first key of the
  index.
*/
int ha_redis::index_read_map(uchar *, const uchar *, key_part_map, enum ha_rkey_function) {
    int rc;
    // DBUG_TRACE;
    rc = HA_ERR_WRONG_COMMAND;
    return rc;
}

/**
  @brief
  Used to read forward through the index.
*/
int ha_redis::index_next(uchar *) {
    int rc;
    // DBUG_TRACE;
    rc = HA_ERR_WRONG_COMMAND;
    return rc;
}

/**
  @brief
  Used to read backwards through the index.
*/
int ha_redis::index_prev(uchar *) {
    int rc;
    // DBUG_TRACE;
    rc = HA_ERR_WRONG_COMMAND;
    return rc;
}

/**
  @brief
  index_first() asks for the first key in the index.
*/
int ha_redis::index_first(uchar *) {
    int rc;
    // DBUG_TRACE;
    rc = HA_ERR_WRONG_COMMAND;
    return rc;
}

/**
  @brief
  index_last() asks for the last key in the index.
*/
int ha_redis::index_last(uchar *) {
    int rc;
    // DBUG_TRACE;
    rc = HA_ERR_WRONG_COMMAND;
    return rc;
}

/**
  @brief
  rnd_init() is called when the system wants the storage engine to do a table
  scan.
*/
int ha_redis::rnd_init(bool) {
    DBUG_ENTER("ha_redis::rnd_init");

    current_position = 0;
    stats.records = 0;

    DBUG_RETURN(0);
}

int ha_redis::rnd_end() {
    DBUG_ENTER("ha_redis::rnd_end");

    // for delete
    std::string cmd = "LREM " + share->table_name + " 0 ,";
    redisReply *rr = (redisReply *)redisCommand(c, cmd.c_str());
    int deleted = rr->integer;
    deleted++; // dummy
    freeReplyObject(rr);

    current_position = 0;
    DBUG_RETURN(0);
}

/**
  @brief
  This is called for each row of the table scan. When you run out of records
  you should return HA_ERR_END_OF_FILE. Fill buff up with the row information.
  The Field structure for the table is the key to getting data into buf
  in a manner that will allow the server to understand it.
*/
int ha_redis::rnd_next(uchar *buf) {
    DBUG_ENTER("ha_redis::rnd_next");
    ha_statistic_increment(&System_status_var::ha_read_rnd_next_count);

    memset(buf, 0, table->s->null_bytes);
    my_bitmap_map *org_bitmap = tmp_use_all_columns(table, table->write_set);

    std::string cmdstr;

    // get length of list
    cmdstr = "LLEN " + share->table_name;
    redisReply *rlen = (redisReply *)redisCommand(c, cmdstr.c_str());
    unsigned long l = rlen->integer;
    if(l == 0 || current_position >= l) {
        freeReplyObject(rlen);
        tmp_restore_column_map(table->write_set, org_bitmap);
        DBUG_RETURN(HA_ERR_END_OF_FILE);
    }
    freeReplyObject(rlen);

    // get values by index
    cmdstr = "LINDEX " + share->table_name + " " + std::to_string(current_position);
    redisReply *rr = (redisReply *)redisCommand(c, cmdstr.c_str());
    std::string r = rr->str;
    buffer.length(0);
    buffer.append(r.c_str());
    freeReplyObject(rr);

    int last_pos = 0;
    for (Field **field = table->field; *field; field++) {
        std::string::size_type pos = r.find(",", last_pos);
        if (pos == std::string::npos) {
            pos = r.length();
        }
        (*field)->store(&buffer[last_pos], pos - last_pos, buffer.charset(), CHECK_FIELD_WARN);
        last_pos = ++pos;
    }

    tmp_restore_column_map(table->write_set, org_bitmap);
    current_position += 1;
    stats.records++;
    DBUG_RETURN(0);
}

/**
  @brief
  position() is called after each call to rnd_next() if the data needs
  to be ordered. You can do something like the following to store
  the position:
  @code
  my_store_ptr(ref, ref_length, current_position);
  @endcode

  @details
  The server uses ref to store data. ref_length in the above case is
  the size needed to store current_position. ref is just a byte array
  that the server will maintain. If you are using offsets to mark rows, then
  current_position should be the offset. If it is a primary key like in
  BDB, then it needs to be a primary key.
*/
void ha_redis::position(const uchar *) {
    my_store_ptr(ref, ref_length, current_position);
}

/**
  @brief
  This is like rnd_next, but you are given a position to use
  to determine the row. The position will be of the type that you stored in
  ref. You can use ha_get_ptr(pos,ref_length) to retrieve whatever key
  or position you saved when position() was called.
*/
int ha_redis::rnd_pos(uchar *buf, uchar *pos) {
    DBUG_ENTER("ha_redis::rnd_pos");
    DBUG_PRINT("buf", ("buf in ha_redis::rnd_pos %s", buf));

    ha_statistic_increment(&System_status_var::ha_read_rnd_count);
    current_position = my_get_ptr(pos, ref_length);

    std::string cmdstr;
    bool read_all = !bitmap_is_clear_all(table->write_set);
    my_bitmap_map *org_bitmap = tmp_use_all_columns(table, table->write_set);
    memset(buf, 0, table->s->null_bytes);

    // get length of list
    cmdstr = "LLEN " + share->table_name;
    redisReply *rlen = (redisReply *)redisCommand(c, cmdstr.c_str());
    unsigned long l = rlen->integer;
    if(l == 0 || current_position > l) {
        freeReplyObject(rlen);
        tmp_restore_column_map(table->write_set, org_bitmap);
        DBUG_RETURN(HA_ERR_END_OF_FILE);
    }
    freeReplyObject(rlen);

    // get values by index
    cmdstr = "LINDEX " + share->table_name + " " + std::to_string(current_position-1);
    redisReply *rr = (redisReply *)redisCommand(c, cmdstr.c_str());
    std::string r = rr->str;
    buffer.length(0);
    buffer.append(r.c_str());
    freeReplyObject(rr);

    int last_pos = 0;
    for (Field **field = table->field; *field; field++) {
        if (read_all || bitmap_is_set(table->read_set, (*field)->field_index)) {
            std::string::size_type p = r.find(",", last_pos);
            if (p == std::string::npos) {
                p = r.length();
            }
            (*field)->store(&buffer[last_pos], p - last_pos, buffer.charset(), CHECK_FIELD_IGNORE);
            last_pos = p+1 ;
        }
    }

    tmp_restore_column_map(table->write_set, org_bitmap);

    DBUG_RETURN(0);
}

/**
  @brief
  ::info() is used to return information to the optimizer. See my_base.h for
  the complete description.

  You will probably want to have the following in your code:
  @code
  if (records < 2)
    records = 2;
  @endcode
  The reason is that the server will optimize for cases of only a single
  record. If, in a table scan, you don't know the number of records, it
  will probably be better to set records to two so you can return as many
  records as you need. Along with records, a few more variables you may wish
  to set are:
    records
    deleted
    data_file_length
    index_file_length
    delete_length
    check_time
  Take a look at the public variables in handler.h for more information.
*/
int ha_redis::info(uint) {
    DBUG_ENTER("ha_redis::info");
    if (stats.records < 2) {
        stats.records = 2;
    }
    DBUG_RETURN(0);
}

/**
  @brief
  extra() is called whenever the server wishes to send a hint to
  the storage engine. The myisam engine implements the most hints.
  ha_innodb.cc has the most exhaustive list of these hints.

    @see
  ha_innodb.cc
*/
int ha_redis::extra(enum ha_extra_function) {
    DBUG_ENTER("ha_redis::extra");
    DBUG_RETURN(0);
}

/**
  @brief
  Used to delete all rows in a table, including cases of truncate and cases
  where the optimizer realizes that all rows will be removed as a result of an
  SQL statement.

  @details
  Called from item_sum.cc by Item_func_group_concat::clear(),
  Item_sum_count_distinct::clear(), and Item_func_group_concat::clear().
  Called from sql_delete.cc by mysql_delete().
  Called from sql_select.cc by JOIN::reinit().
  Called from sql_union.cc by st_select_lex_unit::exec().
*/
int ha_redis::delete_all_rows() {
    DBUG_ENTER("ha_redis::delete_all_rows()");
    // I can't still confirm this truncate() is called
    // when I execute `truncate table ...`, `delete from ... (no where clause) and so on.

    DBUG_RETURN(0);
}

int ha_redis::truncate(dd::Table *) {
    DBUG_ENTER("ha_redis::truncate()");
    // I can't still confirm this truncate() is called when I execute `truncate table ...`

    DBUG_RETURN(0);
}

/**
  @brief
  This create a lock on the table. If you are implementing a storage engine
  that can handle transacations look at ha_berkely.cc to see how you will
  want to go about doing this. Otherwise you should consider calling flock()
  here. Hint: Read the section "locking functions for mysql" in lock.cc to
  understand this.

  @details
  Called from lock.cc by lock_external() and unlock_external(). Also called
  from sql_table.cc by copy_data_between_tables().
*/
int ha_redis::external_lock(THD *, int) {
    DBUG_ENTER("ha_redis::external_lock");
    DBUG_RETURN(0);
}

/**
  @brief
  The idea with handler::store_lock() is: The statement decides which locks
  should be needed for the table. For updates/deletes/inserts we get WRITE
  locks, for SELECT... we get read locks.

  @details
  Before adding the lock into the table lock handler (see thr_lock.c),
  mysqld calls store lock with the requested locks. Store lock can now
  modify a write lock to a read lock (or some other lock), ignore the
  lock (if we don't want to use MySQL table locks at all), or add locks
  for many tables (like we do when we are using a MERGE handler).

  Berkeley DB, for redis, changes all WRITE locks to TL_WRITE_ALLOW_WRITE
  (which signals that we are doing WRITES, but are still allowing other
  readers and writers).

  When releasing locks, store_lock() is also called. In this case one
  usually doesn't have to do anything.

  In some exceptional cases MySQL may send a request for a TL_IGNORE;
  This means that we are requesting the same lock as last time and this
  should also be ignored. (This may happen when someone does a flush
  table when we have opened a part of the tables, in which case mysqld
  closes and reopens the tables and tries to get the same locks at last
  time). In the future we will probably try to remove this.

  Called from lock.cc by get_lock_data().

  @note
  In this method one should NEVER rely on table->in_use, it may, in fact,
  refer to a different thread! (this happens if get_lock_data() is called
  from mysql_lock_abort_for_thread() function)
*/
THR_LOCK_DATA **ha_redis::store_lock(THD *, THR_LOCK_DATA **to,
                                       enum thr_lock_type lock_type) {
    DBUG_ENTER("ha_redis::store_lock");
    if (lock_type != TL_IGNORE && lock.type == TL_UNLOCK) lock.type = lock_type;
    *to++ = &lock;
    DBUG_RETURN(to);
}

/**
  @brief
  Used to delete a table. By the time delete_table() has been called all
  opened references to this table will have been closed (and your globally
  shared references released). The variable name will just be the name of
  the table. You will need to remove any files you have created at this point.

  @details
  If you do not implement this, the default delete_table() is called from
  handler.cc and it will delete all files with the file extensions from
  handlerton::file_extensions.

  Called from handler.cc by delete_table and ha_create_table(). Only used
  during create if the table_flag HA_DROP_BEFORE_CREATE was specified for
  the storage engine.
*/
int ha_redis::delete_table(const char *table_name, const dd::Table *) {
    DBUG_ENTER("ha_redis::delete_table()");
    // Todo: Handlers are already deleted??

    c = redisConnect("127.0.0.1", 6379);
    if (c != NULL && c->err) {
        DBUG_RETURN(-1);
    }
    std::string cmd = "DEL " + get_table_name(table_name);
    redisReply *ret = (redisReply *)redisCommand(c, cmd.c_str());
    if(ret) {
        freeReplyObject(ret);
    }
    redisFree(c);

    DBUG_RETURN(0);
}

/**
  @brief
  Renames a table from one name to another via an alter table call.

  @details
  If you do not implement this, the default rename_table() is called from
  handler.cc and it will delete all files with the file extensions from
  handlerton::file_extensions.
*/
int ha_redis::rename_table(const char *, const char *, const dd::Table *,
                             dd::Table *) {
    // DBUG_TRACE;
    return HA_ERR_WRONG_COMMAND;
}

/**
  @brief
  Given a starting key and an ending key, estimate the number of rows that
  will exist between the two keys.

  @details
  end_key may be empty, in which case determine if start_key matches any rows.
  Called from opt_range.cc by check_quick_keys().
*/
ha_rows ha_redis::records_in_range(uint, key_range *, key_range *) {
    DBUG_ENTER("ha_redis::records_in_range()");
    DBUG_RETURN(10); // low number to force index usage
}

static MYSQL_THDVAR_STR(last_create_thdvar, PLUGIN_VAR_MEMALLOC, NULL, NULL, NULL, NULL);
static MYSQL_THDVAR_UINT(create_count_thdvar, 0, NULL, NULL, NULL, 0, 0, 1000,0);

/**
  @brief
  create() is called to create a database. The variable name will have the name
  of the table.

  @details
  When create() is called you do not need to worry about
  opening the table. Also, the .frm file will have already been
  created so adjusting create_info is not necessary. You can overwrite
  the .frm file at this point if you wish to change the table
  definition, but there are no methods currently provided for doing
  so.
  Called from handle.cc by ha_create_table().
*/
int ha_redis::create(const char *name, TABLE *, HA_CREATE_INFO *, dd::Table *) {
    // Initialize(re-create) table to truncate table.
    c = redisConnect("127.0.0.1", 6379);
    if (c != NULL && c->err) {
        return 0;
    }

    std::string cmd = "DEL " + get_table_name(name);
    redisReply *ret = (redisReply *)redisCommand(c, cmd.c_str());
    if(ret) {
        freeReplyObject(ret);
    }

    /*
      It's just an redis of THDVAR_SET() usage below.
      (This is not changed from example SE)
    */
    THD *thd = ha_thd();
    char *buf = (char *)my_malloc(PSI_NOT_INSTRUMENTED, SHOW_VAR_FUNC_BUFF_SIZE, MYF(MY_FAE));
    snprintf(buf, SHOW_VAR_FUNC_BUFF_SIZE, "Last creation '%s'", name);
    THDVAR_SET(thd, last_create_thdvar, buf);
    my_free(buf);

    uint count = THDVAR(thd, create_count_thdvar) + 1;
    THDVAR_SET(thd, create_count_thdvar, &count);

    return 0;
}

struct st_mysql_storage_engine redis_storage_engine = {MYSQL_HANDLERTON_INTERFACE_VERSION};
static ulong srv_enum_var = 0;
static ulong srv_ulong_var = 0;
static double srv_double_var = 0;
static int srv_signed_int_var = 0;
static long srv_signed_long_var = 0;
static longlong srv_signed_longlong_var = 0;

const char *enum_var_names[] = {"e1", "e2", NullS};

TYPELIB enum_var_typelib = {array_elements(enum_var_names) - 1,
                            "enum_var_typelib", enum_var_names, NULL};

static MYSQL_SYSVAR_ENUM(enum_var,                        // name
                         srv_enum_var,                    // varname
                         PLUGIN_VAR_RQCMDARG,             // opt
                         "Sample ENUM system variable.",  // comment
                         NULL,                            // check
                         NULL,                            // update
                         0,                           // def
                         &enum_var_typelib          // typelib
);

static MYSQL_SYSVAR_ULONG(ulong_var, srv_ulong_var, PLUGIN_VAR_RQCMDARG,
                          "0..1000", NULL, NULL, 8, 0, 1000, 0);

static MYSQL_SYSVAR_DOUBLE(double_var, srv_double_var, PLUGIN_VAR_RQCMDARG,
                           "0.500000..1000.500000", NULL, NULL, 8.5, 0.5,
                           1000.5,
                           0);  // reserved always 0

static MYSQL_THDVAR_DOUBLE(double_thdvar, PLUGIN_VAR_RQCMDARG,
                           "0.500000..1000.500000", NULL, NULL, 8.5, 0.5,
                           1000.5, 0);

static MYSQL_SYSVAR_INT(signed_int_var, srv_signed_int_var, PLUGIN_VAR_RQCMDARG,
                        "INT_MIN..INT_MAX", NULL, NULL, -10, INT_MIN, INT_MAX,
                        0);

static MYSQL_THDVAR_INT(signed_int_thdvar, PLUGIN_VAR_RQCMDARG,
                        "INT_MIN..INT_MAX", NULL, NULL, -10, INT_MIN, INT_MAX,
                        0);

static MYSQL_SYSVAR_LONG(signed_long_var, srv_signed_long_var,
                         PLUGIN_VAR_RQCMDARG, "LONG_MIN..LONG_MAX", NULL, NULL,
                         -10, LONG_MIN, LONG_MAX, 0);

static MYSQL_THDVAR_LONG(signed_long_thdvar, PLUGIN_VAR_RQCMDARG,
                         "LONG_MIN..LONG_MAX", NULL, NULL, -10, LONG_MIN,
                         LONG_MAX, 0);

static MYSQL_SYSVAR_LONGLONG(signed_longlong_var, srv_signed_longlong_var,
                             PLUGIN_VAR_RQCMDARG, "LLONG_MIN..LLONG_MAX", NULL,
                             NULL, -10, LLONG_MIN, LLONG_MAX, 0);

static MYSQL_THDVAR_LONGLONG(signed_longlong_thdvar, PLUGIN_VAR_RQCMDARG,
                             "LLONG_MIN..LLONG_MAX", NULL, NULL, -10, LLONG_MIN,
                             LLONG_MAX, 0);

static SYS_VAR *redis_system_variables[] = {
        MYSQL_SYSVAR(enum_var),
        MYSQL_SYSVAR(ulong_var),
        MYSQL_SYSVAR(double_var),
        MYSQL_SYSVAR(double_thdvar),
        MYSQL_SYSVAR(last_create_thdvar),
        MYSQL_SYSVAR(create_count_thdvar),
        MYSQL_SYSVAR(signed_int_var),
        MYSQL_SYSVAR(signed_int_thdvar),
        MYSQL_SYSVAR(signed_long_var),
        MYSQL_SYSVAR(signed_long_thdvar),
        MYSQL_SYSVAR(signed_longlong_var),
        MYSQL_SYSVAR(signed_longlong_thdvar),
        NULL};

// this is an redis of SHOW_FUNC
static int show_func_redis(MYSQL_THD, SHOW_VAR *var, char *buf) {
    var->type = SHOW_CHAR;
    var->value = buf;  // it's of SHOW_VAR_FUNC_BUFF_SIZE bytes
    snprintf(buf, SHOW_VAR_FUNC_BUFF_SIZE,
             "enum_var is %lu, ulong_var is %lu, "
             "double_var is %f, signed_int_var is %d, "
             "signed_long_var is %ld, signed_longlong_var is %lld",
             srv_enum_var, srv_ulong_var, srv_double_var, srv_signed_int_var,
             srv_signed_long_var, srv_signed_longlong_var);
    return 0;
}

struct redis_vars_t {
    ulong var1;
    double var2;
    char var3[64];
    bool var4;
    bool var5;
    ulong var6;
};

redis_vars_t redis_vars = {100, 20.01, "three hundred", true, 0, 8250};

static SHOW_VAR show_status_redis[] = {
        {"var1", (char *)&redis_vars.var1, SHOW_LONG, SHOW_SCOPE_GLOBAL},
        {"var2", (char *)&redis_vars.var2, SHOW_DOUBLE, SHOW_SCOPE_GLOBAL},
        {0, 0, SHOW_UNDEF, SHOW_SCOPE_UNDEF}  // null terminator required
};

static SHOW_VAR show_array_redis[] = {
        {"array", (char *)show_status_redis, SHOW_ARRAY, SHOW_SCOPE_GLOBAL},
        {"var3", (char *)&redis_vars.var3, SHOW_CHAR, SHOW_SCOPE_GLOBAL},
        {"var4", (char *)&redis_vars.var4, SHOW_BOOL, SHOW_SCOPE_GLOBAL},
        {0, 0, SHOW_UNDEF, SHOW_SCOPE_UNDEF}};

static SHOW_VAR func_status[] = {
        {"redis_func_redis", (char *)show_func_redis, SHOW_FUNC,SHOW_SCOPE_GLOBAL},
        {"redis_status_var5", (char *)&redis_vars.var5, SHOW_BOOL,SHOW_SCOPE_GLOBAL},
        {"redis_status_var6", (char *)&redis_vars.var6, SHOW_LONG,SHOW_SCOPE_GLOBAL},
        {"redis_status", (char *)show_array_redis, SHOW_ARRAY,SHOW_SCOPE_GLOBAL},
        {0, 0, SHOW_UNDEF, SHOW_SCOPE_UNDEF}};

mysql_declare_plugin(redis){
                                     MYSQL_STORAGE_ENGINE_PLUGIN,
                                     &redis_storage_engine,
                                     "REDIS",
                                     "tom__bo",
                                     "Redis storage engine",
                                     PLUGIN_LICENSE_GPL,
                                     redis_init_func, /* Plugin Init */
                                     NULL,              /* Plugin check uninstall */
                                     NULL,              /* Plugin Deinit */
                                     0x0001 /* 0.1 */,
                                     func_status,              /* status variables */
                                     redis_system_variables, /* system variables */
                                     NULL,                     /* config options */
                                     0,                        /* flags */
                             } mysql_declare_plugin_end;
