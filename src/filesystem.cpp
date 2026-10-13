#include "filesystem.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include <sqlite3.h>

namespace {

const std::int64_t ROOT = 1;
const std::uint64_t BS = block_storage::BLOCK_SIZE;
const std::uint64_t MAX_FILE_BYTES = BS << 32;   // block indexes are 32-bit
const std::size_t INDEX_ENTRY_BYTES = 64;        // key, block number, list links and hash node
const unsigned PREFETCH_THREADS = 4;
const std::size_t LOG_BATCH = 4096;
const int READ_AHEAD_MAPPINGS = 32;
const int WARM_UP_READS = 50000;                 // recorded reads version B's detectors are shown at start
const int MODEL_WARM_UP_READS = 8192;            // and how many the model is; it takes longer per read
const double DATA_SHARE = 0.9;                   // part of the cache memory for block data; the rest caches mappings.
                                                 // Fixed in versions A and B; where version C starts
const double GHOST_SHARE = 0.05;                 // each cache remembers what this much more memory would still hold
const std::uint64_t STRETCH = 128;               // version C: block reads between reviews of the memory split
const int PAST_STRETCHES = 512;                  // recorded reviews shown to the model at start
const int PREFETCH_DEPTH = 2;                    // version B: blocks to predict ahead
const double PREFETCH_THRESHOLD = 0.5;           // version B: confidence needed to prefetch

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

// Created on open as well, so images formatted before monitoring existed still work.
const char* LOG_SCHEMA =
    "CREATE TABLE IF NOT EXISTS access_log ("
    "  seq INTEGER PRIMARY KEY, inode INTEGER NOT NULL, idx INTEGER NOT NULL,"
    "  is_write INTEGER NOT NULL);"
    // What version C's reviews of the memory split found: the feedback its model learns from.
    "CREATE TABLE IF NOT EXISTS policy_log ("
    "  seq INTEGER PRIMARY KEY, share REAL NOT NULL, data_cost REAL NOT NULL,"
    "  mapping_cost REAL NOT NULL, read_time REAL NOT NULL)";

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
    statement& bindReal(int i, double v) {
        sqlite3_bind_double(st, i, v);
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
    void reset() { sqlite3_reset(st); }
    std::int64_t integer(int col) { return sqlite3_column_int64(st, col); }
    double real(int col) { return sqlite3_column_double(st, col); }
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

std::int64_t allocateBlock(sqlite3* db) {
    statement q(db, "DELETE FROM free_blocks WHERE block = (SELECT MIN(block) FROM free_blocks) RETURNING block");
    if (!q.step()) fail(ENOSPC, "no free blocks");
    std::int64_t block = q.integer(0);
    q.step();   // run the DELETE to completion
    return block;
}

std::uint64_t nanosSince(std::chrono::steady_clock::time_point start) {
    return std::uint64_t(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - start).count());
}

}  // namespace

filesystem::~filesystem() {
    close();
}

void filesystem::format(const std::string& imagePath, const std::string& dbPath, std::uint32_t blockCount,
                        const fs_options& options) {
    if (db) throw std::logic_error("filesystem already open");
    storage.create(imagePath, blockCount, options.mode);
    try {
        if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) dbFail(db);
        {
            transaction t(db);
            exec(db, SCHEMA);
            statement fill(db,
                           "WITH RECURSIVE n(b) AS (SELECT 0 UNION ALL SELECT b + 1 FROM n WHERE b + 1 < ?)"
                           " INSERT INTO free_blocks SELECT b FROM n");
            fill.bindInt(1, blockCount).step();
            t.commit();
        }
        start(options);
    } catch (...) {
        close();
        std::remove(imagePath.c_str());   // the image was created by this call
        throw;
    }
}

void filesystem::open(const std::string& imagePath, const std::string& dbPath, const fs_options& options) {
    if (db) throw std::logic_error("filesystem already open");
    storage.open(imagePath, options.mode);
    try {
        if (sqlite3_open_v2(dbPath.c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) != SQLITE_OK) dbFail(db);
        {
            statement root(db, "SELECT 1 FROM inodes WHERE id = 1");
            if (!root.step()) throw std::runtime_error(dbPath + " is not a filesystem database");
        }
        start(options);
    } catch (...) {
        close();
        throw;
    }
}

void filesystem::start(const fs_options& chosen) {
    options = chosen;
    exec(db, LOG_SCHEMA);
    guesser = predictor{};
    counters = fs_stats{};
    dataShare = DATA_SHARE;
    stretchReads = stretchNanos = 0;
    atStretchStart = cache_stats{};
    countersAtStretchStart = fs_stats{};
    stretches.clear();
    cache.start(1, options.version == fs_version::baseline ? 0 : PREFETCH_THREADS);
    resize();
    if (options.version == fs_version::baseline) return;
    const bool learned = options.version == fs_version::adaptive;
    if (learned) model.start(options.modelCommand.empty() ? AI_FS_MODEL_COMMAND : options.modelCommand);

    // Show the reads recorded by earlier sessions to whatever does the predicting.
    statement past(db,
                   "SELECT inode, idx FROM (SELECT seq, inode, idx FROM access_log WHERE is_write = 0"
                   " ORDER BY seq DESC LIMIT ?) ORDER BY seq");
    past.bindInt(1, learned ? MODEL_WARM_UP_READS : WARM_UP_READS);
    while (past.step()) {
        const std::int64_t inode = past.integer(0);
        const std::uint64_t idx = std::uint64_t(past.integer(1));
        if (learned) model.learn(inode, idx);
        else guesser.observe(inode, idx, 0);
    }
    if (!learned) return;

    // And the recorded reviews of the memory split.
    statement reviews(db,
                      "SELECT share, data_cost, mapping_cost, read_time FROM (SELECT * FROM policy_log"
                      " ORDER BY seq DESC LIMIT ?) ORDER BY seq");
    reviews.bindInt(1, PAST_STRETCHES);
    while (reviews.step())
        model.report(reviews.real(0), reviews.real(1), reviews.real(2), reviews.real(3), 0, true);
}

void filesystem::close() {
    if (db) flushLog();
    model.stop();
    cache.stop();
    index.clear();
    indexGhost.clear();
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

// Deletes a file or empty directory. The caller holds the transaction.
void filesystem::unlink(std::int64_t id, const std::string& path) {
    statement child(db, "SELECT 1 FROM inodes WHERE parent = ? LIMIT 1");
    if (child.bindInt(1, id).step()) fail(ENOTEMPTY, path);
    {
        statement owned(db, "SELECT block FROM file_blocks WHERE inode = ?");
        owned.bindInt(1, id);
        while (owned.step()) cache.invalidate(std::uint32_t(owned.integer(0)));
    }
    statement(db, "INSERT INTO free_blocks SELECT block FROM file_blocks WHERE inode = ?").bindInt(1, id).step();
    statement(db, "DELETE FROM file_blocks WHERE inode = ?").bindInt(1, id).step();
    statement(db, "DELETE FROM inodes WHERE id = ?").bindInt(1, id).step();
    forgetMappings();
}

void filesystem::remove(const std::string& path) {
    node n = lookup(db, path);
    if (n.id == ROOT) fail(EBUSY, path);
    transaction t(db);
    unlink(n.id, path);
    t.commit();
}

void filesystem::rename(const std::string& from, const std::string& to) {
    node src = lookup(db, from);
    if (src.id == ROOT) fail(EBUSY, from);
    std::vector<std::string> parts = split(to);
    if (parts.empty()) fail(EBUSY, to);
    std::string name = parts.back();
    parts.pop_back();
    node parent = walk(db, parts);
    if (!parent.isDir) fail(ENOTDIR, to);

    // A directory cannot move into itself or anything below it.
    for (std::int64_t at = parent.id; at != ROOT;) {
        if (at == src.id) fail(EINVAL, to);
        statement up(db, "SELECT parent FROM inodes WHERE id = ?");
        if (!up.bindInt(1, at).step()) break;
        at = up.integer(0);
    }

    transaction t(db);
    std::int64_t replaced = 0;
    bool replacedIsDir = false;
    {
        statement old(db, "SELECT id, is_dir FROM inodes WHERE parent = ? AND name = ?");
        if (old.bindInt(1, parent.id).bindText(2, name).step()) {
            replaced = old.integer(0);
            replacedIsDir = old.integer(1) != 0;
        }
    }
    if (replaced == src.id) return;   // onto itself
    if (replaced) {
        if (replacedIsDir != src.isDir) fail(replacedIsDir ? EISDIR : ENOTDIR, to);
        unlink(replaced, to);
    }
    statement move(db, "UPDATE inodes SET parent = ?, name = ? WHERE id = ?");
    move.bindInt(1, parent.id).bindText(2, name).bindInt(3, src.id).step();
    t.commit();
}

void filesystem::truncate(const std::string& path, std::uint64_t size) {
    node n = lookup(db, path);
    if (n.isDir) fail(EISDIR, path);
    if (size > MAX_FILE_BYTES) fail(EFBIG, path);
    if (size == n.size) return;

    transaction t(db);
    if (size < n.size) {
        const std::uint64_t keep = (size + BS - 1) / BS;   // blocks that still hold data
        {
            statement gone(db, "SELECT block FROM file_blocks WHERE inode = ? AND idx >= ?");
            gone.bindInt(1, n.id).bindInt(2, std::int64_t(keep));
            while (gone.step()) cache.invalidate(std::uint32_t(gone.integer(0)));
        }
        statement(db, "INSERT INTO free_blocks SELECT block FROM file_blocks WHERE inode = ? AND idx >= ?")
            .bindInt(1, n.id).bindInt(2, std::int64_t(keep)).step();
        statement(db, "DELETE FROM file_blocks WHERE inode = ? AND idx >= ?")
            .bindInt(1, n.id).bindInt(2, std::int64_t(keep)).step();
        forgetMappings();

        // Bytes past the new end must read as zeros if the file grows again.
        std::int64_t last = size % BS ? blockOf(n.id, keep - 1) : -1;
        if (last >= 0) {
            std::vector<char> buf = cache.read(std::uint32_t(last));
            std::fill(buf.begin() + size % BS, buf.end(), 0);
            cache.write(std::uint32_t(last), buf);
        }
    }
    statement resize(db, "UPDATE inodes SET size = ? WHERE id = ?");
    resize.bindInt(1, std::int64_t(size)).bindInt(2, n.id).step();
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
    if (log.size() >= LOG_BATCH) flushLog();
    node n = lookup(db, path);
    if (n.isDir) fail(EISDIR, path);
    if (data.empty()) return;
    const std::uint64_t end = offset + data.size();
    if (end > MAX_FILE_BYTES || end < offset) fail(EFBIG, path);

    // shortcut: metadata is rolled back on failure but blocks already overwritten
    // are not; needs a journal if the project ever claims crash consistency.
    transaction t(db);
    try {
        for (std::uint64_t pos = offset; pos < end;) {
            const std::uint64_t idx = pos / BS, in = pos % BS;
            const std::uint64_t len = std::min(BS - in, end - pos);
            record(n.id, idx, true);

            std::vector<char> buf(BS, 0);
            std::int64_t block = blockOf(n.id, idx);
            if (block < 0) {
                // A recycled block still holds its old owner's bytes, so start from zeros.
                block = allocateBlock(db);
                statement map(db, "INSERT INTO file_blocks (inode, idx, block) VALUES (?, ?, ?)");
                map.bindInt(1, n.id).bindInt(2, std::int64_t(idx)).bindInt(3, block).step();
                remember(blockKey(n.id, idx), block);
            } else if (len < BS) {
                buf = cache.read(std::uint32_t(block));
            }
            std::copy_n(data.begin() + (pos - offset), len, buf.begin() + in);
            cache.write(std::uint32_t(block), buf);
            pos += len;
        }
        if (end > n.size) {
            statement grow(db, "UPDATE inodes SET size = ? WHERE id = ?");
            grow.bindInt(1, std::int64_t(end)).bindInt(2, n.id).step();
        }
        t.commit();
    } catch (...) {
        forgetMappings();   // they may name blocks the rollback is about to free
        throw;
    }
}

std::vector<char> filesystem::read(const std::string& path, std::uint64_t offset, std::size_t size) {
    if (log.size() >= LOG_BATCH) flushLog();
    node n = lookup(db, path);
    if (n.isDir) fail(EISDIR, path);
    if (offset >= n.size) return {};

    const auto begin = std::chrono::steady_clock::now();
    const std::uint64_t end = offset + std::min<std::uint64_t>(size, n.size - offset);
    std::vector<char> out(end - offset, 0);
    for (std::uint64_t pos = offset; pos < end;) {
        const std::uint64_t idx = pos / BS, in = pos % BS;
        const std::uint64_t len = std::min(BS - in, end - pos);
        record(n.id, idx, false);
        std::int64_t block = blockOf(n.id, idx);
        if (block >= 0) {   // holes stay zero
            std::vector<char> buf = cache.read(std::uint32_t(block));
            std::copy_n(buf.begin() + in, len, out.begin() + (pos - offset));
        }
        if (options.version != fs_version::baseline) anticipate(n.id, idx);
        stretchReads++;
        pos += len;
    }
    stretchNanos += nanosSince(begin);
    if (options.version == fs_version::adaptive && stretchReads >= STRETCH) review();
    return out;
}

fs_stats filesystem::stats() {
    fs_stats s = counters;
    s.cache = cache.stats();
    s.memoryBytes = cache.size() * BS + index.size() * INDEX_ENTRY_BYTES;
    s.threshold = PREFETCH_THRESHOLD;
    s.depth = PREFETCH_DEPTH;
    s.dataShare = dataShare;
    model.stats(s.modelUpdates, s.modelAccuracy);
    return s;
}

// Block number holding block `idx` of the file, or -1 for a hole. With
// readAhead, a miss also loads the mappings that follow in the same query.
std::int64_t filesystem::blockOf(std::int64_t inode, std::uint64_t idx, bool readAhead) {
    const std::uint64_t key = blockKey(inode, idx);
    if (std::int64_t* known = index.get(key)) {
        counters.indexHits++;
        return *known;
    }
    counters.indexMisses++;
    if (indexGhost.contains(key)) counters.indexGhostHits++;

    auto begin = std::chrono::steady_clock::now();
    std::vector<std::int64_t> found(readAhead ? READ_AHEAD_MAPPINGS : 1, -1);
    statement q(db, "SELECT idx, block FROM file_blocks WHERE inode = ? AND idx >= ? AND idx < ?");
    q.bindInt(1, inode).bindInt(2, std::int64_t(idx)).bindInt(3, std::int64_t(idx + found.size()));
    while (q.step()) found[std::size_t(q.integer(0)) - idx] = q.integer(1);
    counters.dbLookups++;
    counters.dbLookupNanos += nanosSince(begin);

    for (std::size_t i = found.size(); i-- > 0;) remember(blockKey(inode, idx + i), found[i]);   // idx ends up newest
    return found[0];
}

void filesystem::remember(std::uint64_t key, std::int64_t block) {
    if (options.version != fs_version::adaptive) {   // only version C needs to know what was dropped
        index.put(key, block);
        return;
    }
    indexGhost.erase(key);
    for (const auto& dropped : index.put(key, block)) indexGhost.put(dropped.first, 0);
}

// shortcut: drops every cached mapping, not only the affected file's; fine
// while deletes and truncates are rare, key the cache per file if they are not.
void filesystem::forgetMappings() {
    index.clear();
    indexGhost.clear();
}

// Prefetches the blocks expected after this read. Version B asks its two
// detectors and keeps the confident guesses. Version C tells the model about
// the read and follows the newest tree the model has trained.
void filesystem::anticipate(std::int64_t inode, std::uint64_t idx) {
    const bool learned = options.version == fs_version::adaptive;
    for (const prediction& p : learned ? model.ask(inode, idx, options.modelWaitMs) : guesser.observe(inode, idx, PREFETCH_DEPTH)) {
        if (p.confidence < PREFETCH_THRESHOLD) continue;
        std::int64_t block = blockOf(p.inode, p.idx, learned);
        if (block >= 0) cache.prefetch(std::uint32_t(block));
    }
}

// Version C: reports how the last stretch of reads went and takes up the
// memory split the model has chosen. Nothing here decides anything; it measures
// and passes on.
//
// The two costs say what a little more memory would have been worth to each
// cache. A cache's ghost list holds what it dropped most recently, as much as
// GHOST_SHARE more memory would still hold. A miss that is on the ghost list
// is one that memory would have avoided, and it cost one miss in that cache: a
// disk read for the block cache, a database query for the mapping cache.
void filesystem::review() {
    const cache_stats now = cache.stats();
    auto each = [](std::uint64_t nanos, std::uint64_t count) { return count ? double(nanos) / count : 0.0; };
    const double diskRead = each(now.diskReadNanos - atStretchStart.diskReadNanos, now.diskReads - atStretchStart.diskReads);
    const double query = each(counters.dbLookupNanos - countersAtStretchStart.dbLookupNanos,
                              counters.dbLookups - countersAtStretchStart.dbLookups);
    const stretch s{dataShare, (now.ghostHits - atStretchStart.ghostHits) * diskRead,
                    (counters.indexGhostHits - countersAtStretchStart.indexGhostHits) * query,
                    double(stretchNanos) / stretchReads};
    stretches.push_back(s);
    counters.splitReports++;
    model.report(s.share, s.dataCost, s.mappingCost, s.readTime, options.modelWaitMs);

    double chosen;
    if (model.newShare(chosen) && chosen != dataShare) {
        dataShare = chosen;
        resize();
    }
    atStretchStart = now;
    countersAtStretchStart = counters;
    stretchReads = stretchNanos = 0;
}

// Splits the memory budget between the two caches.
void filesystem::resize() {
    const double bytes = double(options.cacheBytes);
    auto blocksIn = [&](double share) { return std::max<std::size_t>(1, std::size_t(bytes * share / BS)); };
    auto mappingsIn = [&](double share) {
        return std::max<std::size_t>(1, std::size_t(bytes * share / INDEX_ENTRY_BYTES));
    };
    cache.setCapacity(blocksIn(dataShare));
    cache.setGhostCapacity(blocksIn(GHOST_SHARE));
    indexGhost.setCapacity(mappingsIn(GHOST_SHARE));
    for (const auto& dropped : index.setCapacity(mappingsIn(1 - dataShare))) indexGhost.put(dropped.first, 0);
}

void filesystem::record(std::int64_t inode, std::uint64_t idx, bool isWrite) {
    log.push_back(access{inode, idx, isWrite});
}

// Monitoring must never fail a file operation, so errors only lose log rows.
// shortcut: access_log grows without bound; trim old rows if mounts live long.
void filesystem::flushLog() {
    try {
        if (log.empty() && stretches.empty()) return;
        transaction t(db);
        statement add(db, "INSERT INTO access_log (inode, idx, is_write) VALUES (?, ?, ?)");
        for (const access& a : log) {
            add.bindInt(1, a.inode).bindInt(2, std::int64_t(a.idx)).bindInt(3, a.isWrite).step();
            add.reset();
        }
        statement found(db, "INSERT INTO policy_log (share, data_cost, mapping_cost, read_time) VALUES (?, ?, ?, ?)");
        for (const stretch& r : stretches) {
            found.bindReal(1, r.share).bindReal(2, r.dataCost).bindReal(3, r.mappingCost).bindReal(4, r.readTime).step();
            found.reset();
        }
        t.commit();
    } catch (...) {
    }
    stretches.clear();
    log.clear();
}
