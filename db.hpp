#pragma once
#include <mariadb/mysql.h>
#include <string>
#include "util.hpp"

class DB {
public:
    std::string host, user, pass, dbname;

    bool connect(const char *h, const char *u,
                 const char *p, const char *db)
    {
        host = h; user = u; pass = p; dbname = db;

        // Test one connection
        MYSQL *c = mysql_init(nullptr);
        if (!c) return false;

        if (!mysql_real_connect(c, host.c_str(), user.c_str(),
                                pass.c_str(), dbname.c_str(),
                                0, nullptr, 0))
        {
            log_error("DB connect test failed: " +
                      std::string(mysql_error(c)));
            mysql_close(c);
            return false;
        }

        mysql_close(c);
        log_info("MySQL connected.");
        return true;
    }

private:
    MYSQL* new_conn() {
        MYSQL *c = mysql_init(nullptr);
        if (!c) return nullptr;

        if (!mysql_real_connect(c, host.c_str(), user.c_str(),
                                pass.c_str(), dbname.c_str(),
                                0, nullptr, 0))
        {
            log_error("mysql_real_connect: " +
                      std::string(mysql_error(c)));
            mysql_close(c);
            return nullptr;
        }
        return c;
    }

    std::string esc(MYSQL *c, const std::string &s) {
        std::string out; out.resize(s.size()*2 + 1);
        unsigned long len = mysql_real_escape_string(
            c, &out[0], s.c_str(), s.size());
        out.resize(len);
        return out;
    }

public:
    bool get(const std::string &k, std::string &out, std::string &err)
    {
        MYSQL *c = new_conn();
        if (!c) { err="conn failed"; return false; }

        std::string q =
            "SELECT v FROM kv WHERE k='" + esc(c, k) + "'";

        if (mysql_query(c, q.c_str()) != 0) {
            err = mysql_error(c);
            mysql_close(c);
            return false;
        }

        MYSQL_RES *res = mysql_store_result(c);
        if (!res) {
            err = mysql_error(c);
            mysql_close(c);
            return false;
        }

        MYSQL_ROW row = mysql_fetch_row(res);
        bool ok = (row != nullptr);
        if (ok) out = row[0];

        mysql_free_result(res);
        mysql_close(c);
        return ok;
    }

    bool put(const std::string &k, const std::string &v, std::string &err)
    {
        MYSQL *c = new_conn();
        if (!c) { err="conn failed"; return false; }

        std::string q =
            "INSERT INTO kv(k,v) VALUES('" +
            esc(c, k) + "','" + esc(c, v) +
            "') ON DUPLICATE KEY UPDATE v=VALUES(v)";

        if (mysql_query(c, q.c_str()) != 0) {
            err = mysql_error(c);
            mysql_close(c);
            return false;
        }

        mysql_close(c);
        return true;
    }
};
