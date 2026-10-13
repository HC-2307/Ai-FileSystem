#include "model_client.h"

#include <csignal>
#include <sstream>
#include <stdexcept>

#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const int START_TIMEOUT_MS = 20000;   // Python has to import scikit-learn first
const int REPLY_TIMEOUT_MS = 30000;   // it may still be working through reads it was sent

}  // namespace

model_client::~model_client() {
    stop();
}

void model_client::start(const std::string& command) {
    stop();
    tree.clear();
    recent.clear();
    guessed = false;
    hitRate = 0;
    shareArrived = false;
    signal(SIGPIPE, SIG_IGN);   // writing to a model that died must be an error, not the end of the filesystem

    int down[2], up[2];
    if (pipe(down) != 0) throw std::runtime_error("model: cannot create a pipe");
    if (pipe(up) != 0) {
        close(down[0]);
        close(down[1]);
        throw std::runtime_error("model: cannot create a pipe");
    }
    pid = fork();
    if (pid < 0) {
        for (int fd : {down[0], down[1], up[0], up[1]}) close(fd);
        throw std::runtime_error("model: cannot start a new process");
    }
    if (pid == 0) {
        dup2(down[0], STDIN_FILENO);
        dup2(up[1], STDOUT_FILENO);
        for (int fd = 3; fd < 1024; fd++) close(fd);   // the model must not hold the image or the database open
        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    close(down[0]);
    close(up[1]);
    toModel = down[1];
    fromModel = up[0];

    std::string line;
    if (!receive(line, START_TIMEOUT_MS) || line != "ready") {
        stop();
        throw std::runtime_error("model: could not start `" + command + "`");
    }
}

void model_client::stop() {
    if (toModel >= 0) close(toModel);   // the model sees the end of its input and exits
    if (fromModel >= 0) close(fromModel);
    toModel = fromModel = -1;
    buffer.clear();
    expected = 0;   // a half-received tree is dropped; the tree in use stays
    if (pid > 0) {
        kill(pid, SIGTERM);
        waitpid(pid, nullptr, 0);
    }
    pid = -1;
}

bool model_client::send(const std::string& line) {
    if (!running()) return false;
    std::size_t done = 0;
    while (done < line.size()) {
        const ssize_t n = write(toModel, line.data() + done, line.size() - done);
        if (n <= 0) {
            stop();
            return false;
        }
        done += std::size_t(n);
    }
    return true;
}

// Takes the next complete line the model has sent. Waits up to timeoutMs for
// one (0: only take what is already there). A model that closed its end is
// shut down.
bool model_client::receive(std::string& line, int timeoutMs) {
    while (running()) {
        const std::size_t end = buffer.find('\n');
        if (end != std::string::npos) {
            line = buffer.substr(0, end);
            buffer.erase(0, end + 1);
            return true;
        }
        pollfd waiting{fromModel, POLLIN, 0};
        if (poll(&waiting, 1, timeoutMs) <= 0) return false;   // nothing yet
        char chunk[512];
        const ssize_t n = read(fromModel, chunk, sizeof chunk);
        if (n <= 0) stop();   // gone
        else buffer.append(chunk, std::size_t(n));
    }
    return false;
}

// Takes one line from the model: part of a new tree, or something else to ignore.
void model_client::take(const std::string& line) {
    std::istringstream words(line);
    if (expected == 0) {
        std::string tag;
        double share;
        if (!(words >> tag)) return;
        if (tag == "T" && words >> expected) arriving.clear();
        if (tag == "M" && words >> share && share >= 0.05 && share <= 0.95) {   // anything else is not a usable split
            chosenShare = share;
            shareArrived = true;
        }
        return;
    }
    node n{-1, 0, -1, -1, false, {}};
    words >> n.feature;
    if (n.feature < 0) {
        std::int64_t jump;
        words >> n.pure;
        while (words >> jump) n.jumps.push_back(jump);
    } else {
        words >> n.threshold >> n.left >> n.right;
    }
    arriving.push_back(n);
    if (--expected == 0) tree.swap(arriving);   // complete: start using it
}

void model_client::remember(std::int64_t inode, std::uint64_t idx) {
    recent.emplace_back(inode, std::int64_t(idx));
    if (recent.size() > 3) recent.pop_front();
}

// Follows the tree for the last read. The six inputs are the same ones
// python/model.py trained it on: the file, the block, and the two jumps that
// led here.
std::vector<prediction> model_client::predict() const {
    std::vector<prediction> next;
    if (tree.empty() || recent.empty()) return next;
    const std::size_t last = recent.size() - 1;
    float input[6] = {float(recent[last].first), float(recent[last].second), 0, 0, 0, 0};
    for (std::size_t back = 1; back <= 2 && back <= last; back++) {
        input[2 * back] = float(recent[last - back + 1].first - recent[last - back].first);
        input[2 * back + 1] = float(recent[last - back + 1].second - recent[last - back].second);
    }

    std::size_t at = 0;
    for (std::size_t steps = 0; tree[at].feature >= 0; steps++) {
        const node& n = tree[at];
        if (n.feature >= 6 || steps > tree.size()) return next;   // not a tree this code understands
        const int to = input[n.feature] <= n.threshold ? n.left : n.right;
        if (to < 0 || std::size_t(to) >= tree.size()) return next;
        at = std::size_t(to);
    }
    if (!tree[at].pure) return next;   // its training examples disagreed: no pattern here

    std::int64_t inode = recent[last].first, idx = recent[last].second;
    for (std::size_t i = 0; i + 1 < tree[at].jumps.size(); i += 2) {
        inode += tree[at].jumps[i];
        idx += tree[at].jumps[i + 1];
        if (inode < 0 || idx < 0 || idx > 0xffffffffLL) break;
        next.push_back(prediction{inode, std::uint64_t(idx), 1});
    }
    return next;
}

// Asks the model how many reads it has seen and waits for the answer. Any
// tree it sent before answering has been taken in by the time this returns.
bool model_client::sync(std::uint64_t& seen, int timeoutMs) {
    if (!send("S\n")) return false;
    std::string line;
    while (receive(line, timeoutMs)) {
        std::istringstream words(line);
        std::string tag;
        if (expected == 0 && words >> tag && tag == "S" && words >> seen) return true;
        take(line);
    }
    return false;
}

void model_client::learn(std::int64_t inode, std::uint64_t idx) {
    remember(inode, idx);
    send("L " + std::to_string(inode) + " " + std::to_string(idx) + "\n");
    std::string line;
    while (receive(line, 0)) take(line);   // keep its output moving, or both sides end up waiting on a full pipe
}

std::vector<prediction> model_client::ask(std::int64_t inode, std::uint64_t idx, int waitMs) {
    if (!recent.empty()) hitRate = 0.99 * hitRate + 0.01 * (guessed && guess == std::make_pair(inode, std::int64_t(idx)));
    remember(inode, idx);
    send("R " + std::to_string(inode) + " " + std::to_string(idx) + "\n");
    std::uint64_t seen;
    std::string line;
    if (waitMs > 0) sync(seen, waitMs);
    else while (receive(line, 0)) take(line);

    std::vector<prediction> next = predict();
    guessed = !next.empty();
    if (guessed) guess = {next[0].inode, std::int64_t(next[0].idx)};
    return next;
}

void model_client::report(double share, double dataCost, double mappingCost, double readTime, int waitMs, bool past) {
    std::ostringstream line;
    line << (past ? "H " : "M ") << share << ' ' << dataCost << ' ' << mappingCost << ' ' << readTime << '\n';
    send(line.str());
    std::uint64_t seen;
    std::string answer;
    if (waitMs > 0 && !past) sync(seen, waitMs);
    else while (receive(answer, 0)) take(answer);
}

bool model_client::newShare(double& share) {
    if (!shareArrived) return false;
    shareArrived = false;
    share = chosenShare;
    return true;
}

void model_client::stats(std::uint64_t& seen, double& accuracy) {
    seen = 0;
    accuracy = hitRate;
    if (running() && !sync(seen, REPLY_TIMEOUT_MS)) stop();   // no answer in time
}
