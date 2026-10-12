#include "filesystem.h"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <sqlite3.h>

namespace {

const std::int64_t ROOT = 1;
const std::uint64_t BS = block_storage::BLOCK_SIZE;

const char* SCHEMA =
    "CREATE TABLE inodes ("
    "  id INTEGER PRIMARY KEY, parent INTEGER NOT NULL, name TEXT NOT NULL,"
    "  is_dir INTEGER NOT NULL, size INTEGER NOT NULL DEFAULT 0,"
    "  UNIQUE (parent, name));"
    "CREATE TABLE file_blocks ("
    "  inode INTEGER NOT NULL, idx INTEGER NOT NULL, block INTEGER NOT NULL UNIQUE,"
    "  PRIMARY KEY (inode, idx));"
    "CREATE TABLE free_blocks (block INTEGER PRIMARY KEY);"
    "INSERT INTO inodes (id, parent, name, is_dir) VALUES (1, 0, '', 1);";

[[noreturn]] void fail(int code, const std::string& what) {
    throw std::system_error(code, std::generic_category(), what);
}

[[noreturn]] void dbFail(sqlite3* db) {
    throw std::runtime_error(std::string("sqlite: ") + sqlite3_errmsg(db));
}

void exec(sqlite3* db, const char* sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK) dbFail(db);
}

class statement {
public:
    statement(sqlite3* db, const char* sql) : db(db) {
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) dbFail(db);
    }
    ~statement() { sqlite3_finalize(st); }
    statement(const statement&) = delete;
    statement& operator=(const statement&) = delete;

    statement& bindInt(int i, std::int64_t v) {
        sqlite3_bind_int64(st, i, v);
        return *this;
    }
    statement& bindText(int i, const std::string& v) {
        sqlite3_bind_text(st, i, v.c_str(), -1, SQLITE_TRANSIENT);
        return *this;
    }
    // True when a row is available.
    bool step() {
        int rc = sqlite3_step(st);
        if (rc == SQLITE_ROW) return true;
        if (rc == SQLITE_DONE) return false;
        dbFail(db);
    }
    std::int64_t integer(int col) { return sqlite3_column_int64(st, col); }
    std::string text(int col) { return reinterpret_cast<const char*>(sqlite3_column_text(st, col)); }

private:
    sqlite3* db;
    sqlite3_stmt* st = nullptr;
};

// Rolls back unless commit() is reached.
class transaction {
public:
    explicit transaction(sqlite3* db) : db(db) { exec(db, "BEGIN"); }
    ~transaction() {
        if (!done) sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
    }
    void commit() {
        exec(db, "COMMIT");
        done = true;
    }

private:
    sqlite3* db;
    bool done = false;
};

struct node {
    std::int64_t id;
    bool isDir;
    std::uint64_t size;
};

std::vector<std::string> split(const std::string& path) {
    if (path.empty() || path[0] != '/') fail(EINVAL, "path must be absolute: " + path);
    std::vector<std::string> parts;
    std::stringstream in(path);
    for (std::string part; std::getline(in, part, '/');)
        if (!part.empty()) parts.push_back(part);
    return parts;
}

node walk(sqlite3* db, const std::vector<std::string>& parts) {
    if (!db) throw std::logic_error("filesystem not open");
    node n{ROOT, true, 0};
    for (const std::string& name : parts) {
        if (!n.isDir) fail(ENOTDIR, name);
        statement q(db, "SELECT id, is_dir, size FROM inodes WHERE parent = ? AND name = ?");
        if (!q.bindInt(1, n.id).bindText(2, name).step()) fail(ENOENT, name);
        n = node{q.integer(0), q.integer(1) != 0, std::uint64_t(q.integer(2))};
    }
    return n;
}

node lookup(sqlite3* db, const std::string& path) {
    return walk(db, split(path));
}

// Block number holding block `idx` of the file, or -1 for a hole.
std::int64_t mappedBlock(sqlite3* db, std::int64_t inode, std::uint64_t idx) {
    statement q(db, "SELECT block FROM file_blocks WHERE inode = ? AND idx = ?");
    return q.bindInt(1, inode).bindInt(2, std::int64_t(idx)).step() ? q.integer(0) : -1;
}

std::int64_t allocateBlock(sqlite3* db) {
    statement q(db, "DELETE FROM free_blocks WHERE block = (SELECT MIN(block) FROM free_blocks) RETURNING block");
    if (!q.step()) fail(ENOSPC, "no free blocks");
    std::int64_t block = q.integer(0);
    q.step();   // run the DELETE to completion
    return block;
}

}  // namespace

filesystem::~filesystem() {
    close();
}

void filesystem::format(const std::string& imagePath, const std::string& dbPath, std::uint32_t blockCount,
                        io_mode mode) {
    if (db) throw std::logic_error("filesystem already open");
    storage.create(imagePath, blockCount, mode);
    try {
        if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) dbFail(db);
        transaction t(db);
        exec(db, SCHEMA);
        statement fill(db,
                       "WITH RECURSIVE n(b) AS (SELECT 0 UNION ALL SELECT b + 1 FROM n WHERE b + 1 < ?)"
                       " INSERT INTO free_blocks SELECT b FROM n");
        fill.bindInt(1, blockCount).step();
        t.commit();
    } catch (...) {
        close();
        std::remove(imagePath.c_str());   // the image was created by this call
        throw;
    }
}

void filesystem::open(const std::string& imagePath, const std::string& dbPath, io_mode mode) {
    if (db) throw std::logic_error("filesystem already open");
    storage.open(imagePath, mode);
    try {
        if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) dbFail(db);
        statement root(db, "SELECT 1 FROM inodes WHERE id = 1");
        if (!root.step()) throw std::runtime_error(dbPath + " is not a filesystem database");
    } catch (...) {
        close();
        throw;
    }
}

void filesystem::close() {
    sqlite3_close(db);   // no-op on nullptr
    db = nullptr;
    storage.close();
}

void filesystem::create(const std::string& path, bool isDir) {
    std::vector<std::string> parts = split(path);
    if (parts.empty()) fail(EEXIST, path);
    std::string name = parts.back();
    parts.pop_back();
    node parent = walk(db, parts);
    if (!parent.isDir) fail(ENOTDIR, path);

    statement exists(db, "SELECT 1 FROM inodes WHERE parent = ? AND name = ?");
    if (exists.bindInt(1, parent.id).bindText(2, name).step()) fail(EEXIST, path);
    statement insert(db, "INSERT INTO inodes (parent, name, is_dir) VALUES (?, ?, ?)");
    insert.bindInt(1, parent.id).bindText(2, name).bindInt(3, isDir).step();
}

void filesystem::createFile(const std::string& path) {
    create(path, false);
}

void filesystem::mkdir(const std::string& path) {
    create(path, true);
}

void filesystem::remove(const std::string& path) {
    node n = lookup(db, path);
    if (n.id == ROOT) fail(EBUSY, path);
    statement child(db, "SELECT 1 FROM inodes WHERE parent = ? LIMIT 1");
    if (child.bindInt(1, n.id).step()) fail(ENOTEMPTY, path);

    transaction t(db);
    statement(db, "INSERT INTO free_blocks SELECT block FROM file_blocks WHERE inode = ?").bindInt(1, n.id).step();
    statement(db, "DELETE FROM file_blocks WHERE inode = ?").bindInt(1, n.id).step();
    statement(db, "DELETE FROM inodes WHERE id = ?").bindInt(1, n.id).step();
    t.commit();
}

std::vector<std::string> filesystem::list(const std::string& path) {
    node n = lookup(db, path);
    if (!n.isDir) fail(ENOTDIR, path);
    std::vector<std::string> names;
    statement q(db, "SELECT name FROM inodes WHERE parent = ? ORDER BY name");
    q.bindInt(1, n.id);
    while (q.step()) names.push_back(q.text(0));
    return names;
}

file_info filesystem::stat(const std::string& path) {
    node n = lookup(db, path);
    return file_info{n.isDir, n.size};
}

void filesystem::write(const std::string& path, std::uint64_t offset, const std::vector<char>& data) {
    node n = lookup(db, path);
    if (n.isDir) fail(EISDIR, path);
    if (data.empty()) return;

    // shortcut: metadata is rolled back on failure but blocks already overwritten
    // are not; needs a journal if the project ever claims crash consistency.
    transaction t(db);
    const std::uint64_t end = offset + data.size();
    for (std::uint64_t pos = offset; pos < end;) {
        const std::uint64_t idx = pos / BS, in = pos % BS;
        const std::uint64_t len = std::min(BS - in, end - pos);

        std::vector<char> buf(BS, 0);
        std::int64_t block = mappedBlock(db, n.id, idx);
        if (block < 0) {
            // A recycled block still holds its old owner's bytes, so start from zeros.
            block = allocateBlock(db);
            statement map(db, "INSERT INTO file_blocks (inode, idx, block) VALUES (?, ?, ?)");
            map.bindInt(1, n.id).bindInt(2, std::int64_t(idx)).bindInt(3, block).step();
        } else if (len < BS) {
            buf = storage.readBlock(std::uint32_t(block));
        }
        std::copy_n(data.begin() + (pos - offset), len, buf.begin() + in);
        storage.writeBlock(std::uint32_t(block), buf);
        pos += len;
    }
    if (end > n.size) {
        statement grow(db, "UPDATE inodes SET size = ? WHERE id = ?");
        grow.bindInt(1, std::int64_t(end)).bindInt(2, n.id).step();
    }
    t.commit();
}

std::vector<char> filesystem::read(const std::string& path, std::uint64_t offset, std::size_t size) {
    node n = lookup(db, path);
    if (n.isDir) fail(EISDIR, path);
    if (offset >= n.size) return {};

    const std::uint64_t end = offset + std::min<std::uint64_t>(size, n.size - offset);
    std::vector<char> out(end - offset, 0);
    for (std::uint64_t pos = offset; pos < end;) {
        const std::uint64_t idx = pos / BS, in = pos % BS;
        const std::uint64_t len = std::min(BS - in, end - pos);
        std::int64_t block = mappedBlock(db, n.id, idx);
        if (block >= 0) {   // holes stay zero
            std::vector<char> buf = storage.readBlock(std::uint32_t(block));
            std::copy_n(buf.begin() + in, len, out.begin() + (pos - offset));
        }
        pos += len;
    }
    return out;
}
