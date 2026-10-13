"""The machine-learning model behind version C of the filesystem.

The filesystem starts this program and talks to it through its standard
input and output, one line at a time:

    L <inode> <idx>     a block was read (L: in an earlier session, R: just now)
    R <inode> <idx>
    S                   answer with "S <reads seen>"

The model is a decision tree from scikit-learn (DecisionTreeRegressor). It is
trained on the most recent reads and trained again every so often, so it
follows the workload as it changes.

A "jump" is how far the next read is from the current one. Reading a file
front to back is a jump of +1 block every time. For each read the tree is
given where the read was and the two jumps that led to it, and it predicts the
next four jumps. Adding those to the current block gives the blocks to load
ahead of time.

The tree is only used when it is sure. Every leaf of the tree holds the
training examples that ended up there. If they all continued the same way the
leaf is "pure" and its prediction is used. If they disagree, the reads have no
pattern the tree could find, and nothing is predicted.

Asking this program about every single read would be too slow, so it does the
learning and the filesystem does the predicting. After each training the tree
is written out as plain numbers, one line per node:

    T <number of nodes>
    <feature> <threshold> <left> <right>      a question: if input[feature] <= threshold
                                              go to node <left>, otherwise to node <right>
    -1 <pure> <jump numbers...>               a leaf: pure is 1 or 0, then what it predicts

The filesystem keeps the newest tree and follows it for each read
(src/model_client.cpp). predict() below does the same thing in Python; the
tests use it.
"""
import sys

from sklearn.tree import DecisionTreeRegressor

AHEAD = 4           # how many reads ahead to predict
WINDOW = 2048       # how many recent reads the tree is trained on
RETRAIN = 256       # train again after this many new reads
MIN_LEAF = 2        # a pattern must have been seen twice before the tree trusts it
PURE = 1e-9         # a leaf whose examples differ by less than this counts as pure


def features(reads, t):
    """What the tree is told about read number t: where it was and how it got there."""
    inode, idx = reads[t]
    row = [inode, idx]
    for back in (1, 2):
        if t - back >= 0:
            a, b = reads[t - back], reads[t - back + 1]
            row += [b[0] - a[0], b[1] - a[1]]
        else:
            row += [0, 0]
    return row


class Model:
    def __init__(self):
        self.reads = []         # recent reads, oldest first, as (inode, idx)
        self.tree = None
        self.seen = 0

    def note(self, inode, idx):
        """Records a read. Returns True when that made the model train a new tree."""
        self.reads.append((inode, idx))
        self.reads = self.reads[-WINDOW:]
        self.seen += 1
        # Train often at the start (after 16, 32, 64 and 128 reads), then every RETRAIN reads.
        early = self.seen < RETRAIN and self.seen >= 16 and self.seen & (self.seen - 1) == 0
        if early or self.seen % RETRAIN == 0:
            return self.train()
        return False

    def train(self):
        """Fits a new tree: for every recent read, the AHEAD jumps that followed it."""
        reads = self.reads
        last = len(reads) - AHEAD
        if last < 2:
            return False
        X = [features(reads, t) for t in range(last)]
        y = []
        for t in range(last):
            jumps = []
            for k in range(AHEAD):
                a, b = reads[t + k], reads[t + k + 1]
                jumps += [b[0] - a[0], b[1] - a[1]]
            y.append(jumps)
        self.tree = DecisionTreeRegressor(min_samples_leaf=MIN_LEAF, random_state=1).fit(X, y)
        return True

    def lines(self):
        """The tree as text, one line per node, for the filesystem to follow."""
        t = self.tree.tree_
        out = [f"T {t.node_count}"]
        for node in range(t.node_count):
            if t.children_left[node] == -1:
                pure = t.impurity[node] <= PURE
                jumps = [round(v) for v in t.value[node][:, 0]] if pure else []
                out.append(" ".join(map(str, [-1, int(pure)] + jumps)))
            else:
                out.append(f"{t.feature[node]} {float(t.threshold[node])!r} {t.children_left[node]} {t.children_right[node]}")
        return out

    def predict(self):
        """The blocks expected after the last read, nearest first."""
        if self.tree is None:
            return []
        leaf = self.tree.apply([features(self.reads, len(self.reads) - 1)])[0]
        if self.tree.tree_.impurity[leaf] > PURE:
            return []           # the examples in this leaf disagree: no pattern here
        jumps = self.tree.tree_.value[leaf][:, 0]   # what this leaf predicts: one number per output
        inode, idx = self.reads[-1]
        blocks = []
        for step in range(AHEAD):
            inode += round(jumps[2 * step])
            idx += round(jumps[2 * step + 1])
            if inode < 0 or idx < 0:
                break
            blocks.append((inode, idx))
        return blocks


def serve():
    model = Model()
    print("ready", flush=True)
    for line in sys.stdin:
        words = line.split()
        if not words:
            continue
        if words[0] == "S":
            print("S", model.seen, flush=True)
        elif words[0] in ("L", "R"):
            if model.note(int(words[1]), int(words[2])):
                print("\n".join(model.lines()), flush=True)


if __name__ == "__main__":
    serve()
