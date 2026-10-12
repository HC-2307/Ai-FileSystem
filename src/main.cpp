#define FUSE_USE_VERSION 31
#include "filesystem.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <system_error>
#include <vector>

#include <fuse.h>
#include <unistd.h>

namespace {

filesystem fs;
std::string imagePath, dbPath;
fs_options options;

// Runs one filesystem call and turns what it throws into FUSE's negative errno.
template <class F>
int guarded(F call) {
    try {
        return call();
    } catch (const std::system_error& e) {
        return -e.code().value();
    } catch (const std::exception& e) {
        std::cerr << "ai_fs: " << e.what() << std::endl;
        return -EIO;
    }
}

void printStats() {
    const fs_stats s = fs.stats();
    const std::uint64_t reads = s.cache.hits + s.cache.misses;
    std::cerr << "ai_fs: block reads " << reads << ", cache hits " << s.cache.hits << ", disk reads "
              << s.cache.diskReads << ", disk writes " << s.cache.diskWrites << ", prefetched "
              << s.cache.prefetchIssued << " (used " << s.cache.prefetchUsed << ", wasted "
              << s.cache.prefetchWasted << "), mapping queries " << s.dbLookups << std::endl;
}

// The filesystem is opened here, after FUSE has forked into the background,
// because the prefetch threads would not survive the fork.
void* onInit(fuse_conn_info*, fuse_config* cfg) {
    // Skip the kernel page cache so this filesystem's own cache does the caching.
    cfg->direct_io = 1;
    try {
        fs.open(imagePath, dbPath, options);
    } catch (const std::exception& e) {
        std::cerr << "ai_fs: " << e.what() << std::endl;
        fuse_exit(fuse_get_context()->fuse);
    }
    return nullptr;
}

void onDestroy(void*) {
    printStats();
    fs.close();
}

int onGetattr(const char* path, struct stat* st, fuse_file_info*) {
    return guarded([&] {
        const file_info info = fs.stat(path);
        std::memset(st, 0, sizeof *st);
        st->st_mode = info.isDir ? S_IFDIR | 0755 : S_IFREG | 0644;
        st->st_nlink = info.isDir ? 2 : 1;
        st->st_size = off_t(info.size);
        st->st_blocks = blkcnt_t((info.size + 511) / 512);
        st->st_uid = getuid();
        st->st_gid = getgid();
        return 0;
    });
}

int onReaddir(const char* path, void* buf, fuse_fill_dir_t fill, off_t, fuse_file_info*, fuse_readdir_flags) {
    return guarded([&] {
        const std::vector<std::string> names = fs.list(path);
        fill(buf, ".", nullptr, 0, fuse_fill_dir_flags(0));
        fill(buf, "..", nullptr, 0, fuse_fill_dir_flags(0));
        for (const std::string& name : names) fill(buf, name.c_str(), nullptr, 0, fuse_fill_dir_flags(0));
        return 0;
    });
}

int onMkdir(const char* path, mode_t) {
    return guarded([&] { return fs.mkdir(path), 0; });
}

int onCreate(const char* path, mode_t, fuse_file_info*) {
    return guarded([&] { return fs.createFile(path), 0; });
}

// filesystem::remove takes both kinds; rmdir and unlink must each refuse the other.
int removeKind(const char* path, bool wantDir) {
    return guarded([&] {
        if (fs.stat(path).isDir != wantDir) return wantDir ? -ENOTDIR : -EISDIR;
        return fs.remove(path), 0;
    });
}

int onRmdir(const char* path) {
    return removeKind(path, true);
}

int onUnlink(const char* path) {
    return removeKind(path, false);
}

int onRename(const char* from, const char* to, unsigned int flags) {
    if (flags) return -EINVAL;   // RENAME_NOREPLACE and RENAME_EXCHANGE are not supported
    return guarded([&] { return fs.rename(from, to), 0; });
}

int onTruncate(const char* path, off_t size, fuse_file_info*) {
    return guarded([&] { return fs.truncate(path, std::uint64_t(size)), 0; });
}

int onOpen(const char* path, fuse_file_info*) {
    return guarded([&] { return fs.stat(path).isDir ? -EISDIR : 0; });
}

int onRead(const char* path, char* buf, size_t size, off_t offset, fuse_file_info*) {
    return guarded([&] {
        const std::vector<char> data = fs.read(path, std::uint64_t(offset), size);
        std::memcpy(buf, data.data(), data.size());
        return int(data.size());
    });
}

int onWrite(const char* path, const char* buf, size_t size, off_t offset, fuse_file_info*) {
    return guarded([&] {
        fs.write(path, std::uint64_t(offset), std::vector<char>(buf, buf + size));
        return int(size);
    });
}

// Timestamps are not stored; accepting the call keeps `touch` and `cp -p` quiet.
int onUtimens(const char*, const struct timespec[2], fuse_file_info*) {
    return 0;
}

int usage() {
    std::cerr << "usage: ai_fs format <image> <db> <blocks>\n"
                 "       ai_fs mount <image> <db> <mountpoint> [--version A|B|C] [--cache-mb N] [--direct]"
                 " [FUSE options]\n"
                 "  A: plain LRU caches   B: adds prediction and prefetching   C: adds feedback (default)\n"
                 "  --direct opens the image with O_DIRECT (not available on tmpfs)\n";
    return 2;
}

std::string absolute(const char* path) {
    char resolved[PATH_MAX];
    if (!realpath(path, resolved)) throw std::system_error(errno, std::generic_category(), path);
    return resolved;
}

int mount(int argc, char** argv) {
    imagePath = absolute(argv[2]);   // FUSE changes directory when it goes to the background
    dbPath = absolute(argv[3]);
    options.version = fs_version::adaptive;

    std::vector<char*> fuseArgs = {argv[0], argv[4]};
    for (int i = 5; i < argc; i++) {
        const std::string arg = argv[i];
        if (arg == "--direct") {
            options.mode = io_mode::direct;
        } else if (arg == "--version" && i + 1 < argc) {
            const std::string v = argv[++i];
            if (v != "A" && v != "B" && v != "C") return usage();
            options.version = fs_version(v[0] - 'A');
        } else if (arg == "--cache-mb" && i + 1 < argc) {
            const long mb = std::strtol(argv[++i], nullptr, 10);
            if (mb <= 0) return usage();
            options.cacheBytes = std::size_t(mb) << 20;
        } else {
            fuseArgs.push_back(argv[i]);
        }
    }
    static char single[] = "-s";   // filesystem is not thread-safe
    fuseArgs.push_back(single);

    // Fail here, with a message on the terminal, rather than after the fork.
    fs.open(imagePath, dbPath, options);
    fs.close();

    fuse_operations ops = {};
    ops.init = onInit;
    ops.destroy = onDestroy;
    ops.getattr = onGetattr;
    ops.readdir = onReaddir;
    ops.mkdir = onMkdir;
    ops.rmdir = onRmdir;
    ops.create = onCreate;
    ops.unlink = onUnlink;
    ops.rename = onRename;
    ops.truncate = onTruncate;
    ops.open = onOpen;
    ops.read = onRead;
    ops.write = onWrite;
    ops.utimens = onUtimens;
    return fuse_main(int(fuseArgs.size()), fuseArgs.data(), &ops, nullptr);
}

}  // namespace

int main(int argc, char** argv) {
    try {
        const std::string command = argc > 1 ? argv[1] : "";
        if (command == "format" && argc == 5) {
            const long blocks = std::strtol(argv[4], nullptr, 10);
            if (blocks <= 0 || blocks > long(UINT32_MAX)) return usage();
            fs.format(argv[2], argv[3], std::uint32_t(blocks));
            return 0;
        }
        if (command == "mount" && argc >= 5) return mount(argc, argv);
        return usage();
    } catch (const std::exception& e) {
        std::cerr << "ai_fs: " << e.what() << std::endl;
        return 1;
    }
}
