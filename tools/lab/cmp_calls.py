"""Compares two traces of random draws (in2net-rngcalls-A/B.bin): the first draw that differs."""
import array, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lab
copy = sys.argv[1] if len(sys.argv) > 1 else os.path.join(lab.WORK, "test-in2")
def load(tag):
    a = array.array("I")
    p = os.path.join(copy, "in2net-rngcalls-%s.bin" % tag)
    a.frombytes(open(p, "rb").read())
    return a
a, b = load("A"), load("B")
n = min(len(a), len(b)) // 2
print("draws: A %d, B %d" % (len(a) // 2, len(b) // 2))
other = sum(1 for i in range(1, len(a), 2) if a[i] & 0x80000000)
print("draws from other threads in A:", other)
first = None
if a[:2 * n] == b[:2 * n]:
    print("the common part is identical")
else:
    lo, hi = 0, n
    while lo < hi:  # first index where the prefixes differ
        mid = (lo + hi) // 2
        if a[:2 * mid + 2] == b[:2 * mid + 2]:
            lo = mid + 1
        else:
            hi = mid
    first = lo
    print("first difference at draw %d" % first)
    for k in range(max(0, first - 8), min(n, first + 10)):
        print("  %s A f%-6d %08X   B f%-6d %08X" % ("*" if (a[2 * k], a[2 * k + 1]) != (b[2 * k], b[2 * k + 1]) else " ",
                                                    a[2 * k], a[2 * k + 1], b[2 * k], b[2 * k + 1]))
