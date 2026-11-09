"""Checks that the drawn schematic has exactly the connections in design.py.

Usage (inside the KiCad image, after gen_sch.py):
  kicad-cli sch export netlist --format kicadsexpr -o /tmp/cal.net calibrator.kicad_sch
  python3 scripts/check_netlist.py /tmp/cal.net
"""
import re
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
import design as D  # noqa: E402

text = open(sys.argv[1]).read()
drawn = set()
for chunk in text.split("(net (code")[1:]:
    name = re.search(r'\(name "([^"]*)"', chunk).group(1)
    nodes = frozenset(n for n in re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', chunk)
                      if not n[0].startswith("#"))
    if nodes and not name.startswith("unconnected"):
        drawn.add(nodes)
want = {}
for ref, p in D.PARTS.items():
    for pin, net in p[4].items():
        if net:
            want.setdefault(net, set()).add((ref, pin))
want = {frozenset(v) for v in want.values()}
drawn = {d for d in drawn if len(d) > 0}
missing = want - drawn
extra = drawn - want
for s in missing:
    print("in design.py but not drawn:", sorted(s))
for s in extra:
    print("drawn but not in design.py:", sorted(s))
print("netlist check:", "OK" if not missing and not extra else "MISMATCH")
sys.exit(1 if missing or extra else 0)
