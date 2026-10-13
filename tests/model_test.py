"""Tests for python/model.py. Run with the Python that has scikit-learn."""
import random
import subprocess
import sys
from pathlib import Path

MODEL = Path(__file__).resolve().parent.parent / "python" / "model.py"
sys.path.insert(0, str(MODEL.parent))
from model import AHEAD, HIGHEST, LOWEST, WINDOW, Model, Split, features  # noqa: E402


def read(model, inode, idx):
    model.note(inode, idx)
    return model.predict()


def scatter(model, blocks=2000, reads=4000):
    """Reads in no repeating order. Returns how many predictions were made."""
    x, made = 12345, 0
    for _ in range(reads):
        x = (x * 1664525 + 1013904223) & 0xFFFFFFFF
        made += len(read(model, 1, (x >> 16) % blocks))
    return made


def test_predicts_nothing_before_it_has_trained():
    m = Model()
    assert all(read(m, 1, i) == [] for i in range(15))


def test_learns_a_sequential_scan():
    m = Model()
    for i in range(48):
        read(m, 1, i)
    assert read(m, 1, 48) == [(1, 49 + k) for k in range(AHEAD)]
    assert m.seen == 49


def test_a_scan_learned_on_one_file_works_on_another():
    m = Model()
    for i in range(48):
        read(m, 1, i)
    read(m, 2, 0)
    assert read(m, 2, 1)[:1] == [(2, 2)]


def test_learns_a_repeating_order():
    m = Model()
    for _ in range(14):                              # 70 reads; the tree is trained at 16, 32 and 64
        for idx in (40, 7, 23, 91, 15):
            read(m, 1, idx)
    assert read(m, 1, 40)[:2] == [(1, 7), (1, 23)]


def test_stays_quiet_on_random_reads():
    m = Model()
    assert scatter(m) < 40


def test_recovers_after_random_reads():
    m = Model()
    scatter(m)
    right = [i for i in range(600) if read(m, 3, i)[:1] == [(3, i + 1)]]
    assert right and right[0] < 300                  # it has to wait for the next training
    assert len(right) > 250


def test_keeps_only_recent_reads():
    m = Model()
    for i in range(WINDOW + 500):
        m.note(1, i)
    assert len(m.reads) == WINDOW


def follow(lines, row):
    """Follows a tree written by Model.lines(), the way the filesystem does."""
    assert lines[0] == f"T {len(lines) - 1}"
    nodes = [line.split() for line in lines[1:]]
    at = 0
    while nodes[at][0] != "-1":
        feature, threshold, left, right = nodes[at]
        at = int(left) if row[int(feature)] <= float(threshold) else int(right)
    return [int(v) for v in nodes[at][2:]] if nodes[at][1] == "1" else None


def test_the_written_tree_predicts_the_same_as_the_model():
    m = Model()
    x, checked, quiet = 99, 0, 0
    for i in range(1200):
        x = (x * 1664525 + 1013904223) & 0xFFFFFFFF
        # stretches of a repeating order and stretches of random reads, so there are pure and impure leaves
        inode, idx = (1 + i % 3, (i * 37) % 200) if i % 200 < 150 else (5, (x >> 8) % 3000)
        m.note(inode, idx)
        if m.tree is None:
            continue
        jumps = follow(m.lines(), features(m.reads, len(m.reads) - 1))
        blocks = []
        for step in range(AHEAD if jumps else 0):
            inode, idx = inode + jumps[2 * step], idx + jumps[2 * step + 1]
            if inode < 0 or idx < 0:
                break
            blocks.append((inode, idx))
        assert blocks == m.predict()
        checked += bool(blocks)
        quiet += not blocks
    assert checked > 100 and quiet > 100


def test_split_learns_which_move_makes_reads_faster():
    r, split = random.Random(1), Split()
    share, read_time, right, asked = 0.7, 1000.0, 0, 0
    for i in range(400):
        short = r.choice((1, -1))                    # which cache is short of memory this time
        pressure = split.note(share, 1000 if short > 0 else 0, 0 if short > 0 else 1000, read_time)
        assert pressure == short
        new = min(0.9, max(0.55, split.choose(share, pressure)))    # leave room to move both ways
        move = (new > share + 1e-9) - (new < share - 1e-9)
        share = new
        read_time = 1000 - 200 * move * short + r.gauss(0, 60)      # helping the short cache makes reads faster
        if i >= 40:
            asked += 1
            right += move == short
    assert right > asked * 0.8                       # measured: 94%


def test_split_stays_in_range_whatever_it_is_told():
    split = Split()
    share = 0.9
    for i in range(300):
        pressure = split.note(share, [0, 1e15, 3][i % 3], [0, 0, 1e15][i % 3], [0, 1e12, 5][i % 3])
        assert -1 <= pressure <= 1
        share = split.choose(share, pressure)
        assert LOWEST <= share <= HIGHEST


def test_speaks_the_protocol():
    lines = (["L 1 %d" % i for i in range(20)] + ["S", "R 1 20", "nonsense", "", "M 0.9", "S"]
             + ["H 0.9 5 0 100", "M 0.9 0 5 100", "M 0.85 0 0 90"])
    out = subprocess.run([sys.executable, str(MODEL)], input="\n".join(lines) + "\n",
                         capture_output=True, text=True, timeout=60).stdout.splitlines()
    assert out[0] == "ready"
    count = int(out[1].split()[1])                   # the tree trained after 16 reads
    assert out[1].split()[0] == "T" and len(out) == 1 + 1 + count + 2 + 2
    assert follow(out[1:2 + count], [1, 15, 0, 1, 0, 1]) == [0, 1] * AHEAD
    assert out[2 + count] == "S 20" and out[3 + count] == "S 21"     # lines it cannot use get no answer
    for answer in out[4 + count:]:                   # H gets no answer; each M gets a split
        tag, share = answer.split()
        assert tag == "M" and LOWEST <= float(share) <= HIGHEST


if __name__ == "__main__":
    for name, test in list(globals().items()):
        if name.startswith("test_"):
            print(name, flush=True)
            test()
