# mxl-avsync. MXL_REF is the platform pin; this image builds the core, file tools and serve
# path verified without libmxl. A host image that links libmxl uses the same pin.
FROM ubuntu:24.04
ARG MXL_REF=218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7
LABEL org.opencontainers.image.title="mxl-avsync" \
      org.opencontainers.image.source="https://github.com/LeeO86/mxl-avsync" \
      org.opencontainers.image.revision="${MXL_REF}"
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build pkg-config ca-certificates \
      libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build -j"$(nproc)" \
    && cmake --install build || true \
    && cp build/mxl-avsync /usr/local/bin/mxl-avsync
EXPOSE 8180 3332
ENV AVSYNC_STATE_DIR=/config
ENTRYPOINT ["/usr/local/bin/mxl-avsync"]
CMD ["serve"]
