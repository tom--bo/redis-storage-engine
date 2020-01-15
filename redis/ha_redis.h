/* Copyright (c) 2004, 2017, Oracle and/or its affiliates. All rights reserved.

  This program is free software; you can redisribute it and/or modify
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

/** @file ha_redis.h

    @brief
  The ha_redis engine is a sample storage engine for learning MySQL more.
  Starting from example storage engine, This engine uses Redis for backend.
  Its purpose is to understand MySQL deeper and just for fun!

   @see
  /sql/handler.h and /storage/redis/ha_redis.cc
*/

#include <sys/types.h>

#include "my_base.h" /* ha_rows */
#include "my_compiler.h"
#include "my_inttypes.h"
#include "sql/handler.h" /* handler */
#include "include/sql_string.h"
#include "thr_lock.h"    /* THR_LOCK, THR_LOCK_DATA */

#include "hiredis.h" /* for redis */

/** @brief
  Redis_share is a class that will be shared among all open handlers.
  This redis implements the minimum of what you will probably need.
*/
class Redis_share : public Handler_share {
public:
    THR_LOCK lock;
    std::string table_name;
    Redis_share();
    ~Redis_share() { thr_lock_delete(&lock); }
};

/** @brief
  Class definition for the storage engine
*/
class ha_redis : public handler {
    THR_LOCK_DATA lock;        ///< MySQL lock
    Redis_share *share;        ///< Shared lock info
    Redis_share *get_share();  ///< Get the share

    redisContext *c;
    unsigned long current_position;
    String buffer;

public:
    ha_redis(handlerton *hton, TABLE_SHARE *table_arg);
    ~ha_redis() {}
    const char *table_type() const { return "REDIS"; }

    /**
      Replace key algorithm with one supported by SE, return the default key
      algorithm for SE if explicit key algorithm was not provided.

      @sa handler::adjust_index_algorithm().
    */
    virtual enum ha_key_alg get_default_index_algorithm() const {
        return HA_KEY_ALG_HASH;
    }

    virtual bool is_index_algorithm_supported(enum ha_key_alg key_alg) const {
        return key_alg == HA_KEY_ALG_HASH;
    }

    /** @brief
      This is a list of flags that indicate what functionality the storage engine
      implements. The current table flags are documented in handler.h
    */
    ulonglong table_flags() const {
        return HA_BINLOG_STMT_CAPABLE;
    }

    /** @brief
      This is a bitmap of flags that indicates how the storage engine
      implements indexes. The current index flags are documented in
      handler.h. If you do not implement indexes, just return zero here.

        @details
      part is the key part to check. First key part is 0.
      If all_parts is set, MySQL wants to know the flags for the combined
      index, up to and including 'part'.
    */
    ulong index_flags(uint inx MY_ATTRIBUTE((unused)),
                      uint part MY_ATTRIBUTE((unused)),
                      bool all_parts MY_ATTRIBUTE((unused))) const {
        return 0;
    }

    /** @brief
     * retrieve table name from path
     */
    std::string get_table_name(const char *tname) {
        std::string s = tname;
        std::string::size_type pos = s.find_last_of("/");
        if(pos != std::string::basic_string::npos) {
            return s.substr(pos+1, s.length()-(pos+1)).c_str();
        }
        return tname;
    }

    /** @brief
      unireg.cc will call max_supported_record_length(), max_supported_keys(),
      max_supported_key_parts(), uint max_supported_key_length()
      to make sure that the storage engine can handle the data it is about to
      send. Return *real* limits of your storage engine here; MySQL will do
      min(your_limits, MySQL_limits) automatically.
     */
    uint max_supported_record_length() const { return HA_MAX_REC_LENGTH; }

    /** @brief
      unireg.cc will call this to make sure that the storage engine can handle
      the data it is about to send. Return *real* limits of your storage engine
      here; MySQL will do min(your_limits, MySQL_limits) automatically.

        @details
      There is no need to implement ..._key_... methods if your engine doesn't
      support indexes.
     */
    uint max_supported_keys() const { return 0; }

    /** @brief
      unireg.cc will call this to make sure that the storage engine can handle
      the data it is about to send. Return *real* limits of your storage engine
      here; MySQL will do min(your_limits, MySQL_limits) automatically.

        @details
      There is no need to implement ..._key_... methods if your engine doesn't
      support indexes.
     */
    uint max_supported_key_parts() const { return 0; }

    /** @brief
      unireg.cc will call this to make sure that the storage engine can handle
      the data it is about to send. Return *real* limits of your storage engine
      here; MySQL will do min(your_limits, MySQL_limits) automatically.

        @details
      There is no need to implement ..._key_... methods if your engine doesn't
      support indexes.
     */
    uint max_supported_key_length() const { return 0; }

    /** @brief
      Called in test_quick_select to determine if indexes should be used.
    */
    virtual double scan_time() {
        return (double)(stats.records + stats.deleted) / 2.0 + 100;
        // Experimentally, I return big score to force using index (I'm not sure it works well...)
        // below is original (example se)
        // return (double)(stats.records + stats.deleted) / 20.0 + 10;
    }

    /** @brief
      This method will never be called if you do not implement indexes.
    */
    virtual double read_time(uint, uint, ha_rows rows) {
        return (double)rows / 20.0 + 1;
    }

    /*
      Everything below are methods that we implement in ha_redis.cc.
    */
    int create(const char *name, TABLE *form, HA_CREATE_INFO *create_info, dd::Table *table_def);  ///< required
    int open(const char *name, int mode, uint test_if_locked, const dd::Table *table_def); ///< required
    int close(void); ///< required
    int write_row(uchar *buf);
    int update_row(const uchar *old_data, uchar *new_data);
    int delete_row(const uchar *buf);

    /** @brief
      Unlike index_init(), rnd_init() can be called two consecutive times
      without rnd_end() in between (it only makes sense if scan=1). In this
      case, the second call should prepare for the new table scan (e.g if
      rnd_init() allocates the cursor, the second call should position the
      cursor to the start of the table; no need to deallocate and allocate
      it again. This is a required method.
    */
    int rnd_init(bool scan);              ///< required
    int rnd_end();                        ///< required
    int rnd_next(uchar *buf);             ///< required
    int rnd_pos(uchar *buf, uchar *pos);  ///< required
    void position(const uchar *record);   ///< required
    int info(uint);                       ///< required
    int extra(enum ha_extra_function operation);
    int external_lock(THD *thd, int lock_type);  ///< required
    int delete_all_rows(void);
    int truncate(dd::Table *);
    ha_rows records_in_range(uint inx, key_range *min_key, key_range *max_key);
    int delete_table(const char *from, const dd::Table *table_def);
    int rename_table(const char *from, const char *to, const dd::Table *from_table_def, dd::Table *to_table_def);
    THR_LOCK_DATA **store_lock(THD *thd, THR_LOCK_DATA **to, enum thr_lock_type lock_type);  ///< required
    /** @brief
      We implement below methods in ha_redis.cc. It's not an obligatory method;
      skip it and and MySQL will treat it as not implemented.
    */
    int index_read_map(uchar *buf, const uchar *key, key_part_map keypart_map, enum ha_rkey_function find_flag);
    int index_next(uchar *buf);
    int index_prev(uchar *buf);
    int index_first(uchar *buf);
    int index_last(uchar *buf);
};
