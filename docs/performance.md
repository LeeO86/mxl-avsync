# Performance

Drawing the 1080p timing picture (background fill, static layer, marker, strip, sweep, one text line) took **0.207 ms per frame** on this VM (`avsync-core-tests`, case "1080p drawing cost", `draw_1080p_ms=0.206943`). That is under the 1 ms/frame target. The machine was 4 CPUs, `Intel(R) Xeon(R) Processor`. It is not a claim about a Skylake-SP server or an 8-core AVX2 desktop. The figure does not include a QR encode.

The file tools spend most of their time in ProRes or H.264, not in the marker. A 3 s 1080p50 ProRes round trip completes in well under a minute on this 4-core VM.

Catalog CPU requests stay at the spec's placeholders (500m request, 1 CPU limit) until the host measurement in M5 replaces them.
