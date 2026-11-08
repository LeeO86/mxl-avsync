# Vendored files

| File | What | Source | Licence |
| --- | --- | --- | --- |
| `ch32x035_regs.h` | Minimal CH32X035 register map (only the registers this firmware uses) | Structs, base addresses and bit values taken from `ch32fun/ch32x03xhw.h` of [cnlohr/ch32fun](https://github.com/cnlohr/ch32fun), commit `b28d285460ce052a179dba8f4a22c6f73ff8e328` (2026-10-31), downloaded 2026-11-08. The comparator bit positions in `OPA->CTLR2` follow `OPA_CMP_Init()` in WCH's [openwch/ch32x035](https://github.com/openwch/ch32x035) EVT (`ch32x035_opa.c` V1.0.1); no WCH code is copied. | MIT, see `LICENSE-MIT-ch32fun.txt` |
| `LICENSE-MIT-ch32fun.txt` | Licence text of ch32fun | same repository | MIT |

The MIT licence fits this MIT firmware. Keep the licence notice with the header.
No scripts or build files from those repositories are used.
