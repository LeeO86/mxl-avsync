# Performance

Drawing the 1080p timing picture (background fill, static layer, marker, strip, sweep, one text line) took **0.207 ms per frame** on this VM (`avsync-core-tests`, case "1080p drawing cost", `draw_1080p_ms=0.206943`). That is under the 1 ms/frame target. The machine was 4 CPUs, `Intel(R) Xeon(R) Processor`. It is not a claim about a Skylake-SP server or an 8-core AVX2 desktop. The figure does not include a QR encode.

The file tools spend most of their time in ProRes or H.264, not in the marker. A 3 s 1080p50 ProRes round trip completes in well under a minute on this 4-core VM.

Catalog requests for a Kubernetes production platform are 700m CPU and 256Mi memory, with limits 1500m and 384Mi. Measured at 1080p50, 2 events/s, for 60 s: the generator used 0.54 core, RSS 135.6 MB (cgroup 97 MiB), 57 threads, and 53 MB of tmpfs. The analyzer used 0.54 core, RSS 158.7 MB (cgroup 71 MiB), 60 threads, and 7 MB of tmpfs. There is no second measurement.

## Live generator per grain (2.0.4)

`avsync-render-bench [format] [grains]` times the live picture: a full `render_picture` + `pack_v210`, and the incremental `LivePicture`, and checks that both give the same bytes. On the lab host (idle core): 1080p50 full 7.9 ms p50, live 0.84 ms p50 / 3.7 ms p99; 720p50 4.1 / 0.44 ms; 2160p50 33.8 / 3.3 ms. Under a node-like load (two bursty busy loops per generator CPU, four memory-bandwidth threads) the live generator used 0.14 core and `mxl_avsync_generator_render_seconds` had a mean of 2.4 ms.
