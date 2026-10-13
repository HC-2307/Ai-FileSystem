#pragma once
#include "predictor.h"

#include <cstdint>
#include <deque>
#include <string>
#include <utility>
#include <vector>

#include <sys/types.h>

// The filesystem's side of version C's machine-learning model.
//
// The learning is done by python/model.py, which this class runs as a
// separate program and talks to through two pipes. It is told about every
// read, and every so often it sends back a newly trained decision tree as
// plain numbers. This class keeps the newest tree and follows it to predict
// the blocks that will be read next. Following a tree takes well under a
// microsecond, so a read never waits for Python.
//
// If the program goes away, the last tree it sent stays in use.
class model_client {
public:
    model_client() = default;
    ~model_client();
    model_client(const model_client&) = delete;
    model_client& operator=(const model_client&) = delete;

    // Starts `command` with /bin/sh. Throws std::runtime_error if it does not say "ready".
    void start(const std::string& command);
    void stop();
    bool running() const { return pid > 0; }

    // A read recorded by an earlier session: the model is told, nothing is predicted.
    void learn(std::int64_t inode, std::uint64_t idx);
    // A read that just happened: tells the model and returns the blocks the
    // current tree expects next. With waitMs above 0 it first waits, up to
    // that long, until the model has taken this read in and sent any new tree.
    std::vector<prediction> ask(std::int64_t inode, std::uint64_t idx, int waitMs = 0);
    // How many reads the model has been told about, and the running share of
    // reads that were the tree's first guess.
    void stats(std::uint64_t& seen, double& accuracy);

private:
    // One line of the tree python/model.py sends. A question node says which
    // input to compare with `threshold` and where to go next; a leaf holds the
    // jumps it predicts, or is marked not pure and predicts nothing.
    struct node {
        int feature;                      // -1 for a leaf
        double threshold;
        int left, right;
        bool pure;
        std::vector<std::int64_t> jumps;  // (inodes along, blocks along) for each read ahead
    };

    bool send(const std::string& line);
    bool receive(std::string& line, int timeoutMs);
    void take(const std::string& line);
    void remember(std::int64_t inode, std::uint64_t idx);
    std::vector<prediction> predict() const;
    bool sync(std::uint64_t& seen, int timeoutMs);

    pid_t pid = -1;
    int toModel = -1, fromModel = -1;
    std::string buffer;                   // bytes received after the last complete line

    std::vector<node> tree, arriving;     // the tree in use, and one still being received
    std::size_t expected = 0;             // nodes of `arriving` not received yet
    std::deque<std::pair<std::int64_t, std::int64_t>> recent;   // the last three reads, oldest first
    bool guessed = false;                 // the tree named a block to follow the last read
    std::pair<std::int64_t, std::int64_t> guess;
    double hitRate = 0;
};
