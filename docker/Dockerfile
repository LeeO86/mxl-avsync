# mxl-avsync. MXL_REF is the platform pin; this image builds the core, file tools and serve
# path verified without libmxl. A host image that links libmxl uses the same pin.
FROM ubuntu:24.04
ARG MXL_REF=218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7
LABEL org.opencontainers.image.title="mxl-avsync" \
      org.opencontainers.image.source="https://github.com/LeeO86/mxl-avsync" \
      org.opencontainers.image.revision="${MXL_REF}"
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build pkg-config ca-certificates git python3 \
      curl zip unzip tar bison flex autoconf automake libtool \
      libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
    && rm -rf /var/lib/apt/lists/*
RUN git clone --depth 1 https://github.com/microsoft/vcpkg /opt/vcpkg \
    && /opt/vcpkg/bootstrap-vcpkg.sh -disableMetrics
RUN git init /tmp/mxl \
    && git -C /tmp/mxl remote add origin https://github.com/dmf-mxl/mxl.git \
    && git -C /tmp/mxl fetch --depth 1 origin "${MXL_REF}" \
    && git -C /tmp/mxl checkout FETCH_HEAD
COPY docker/mxl-vcpkg.json /tmp/mxl-manifest/vcpkg.json
RUN BASELINE="$(git -C /opt/vcpkg rev-parse HEAD)" \
    && sed -i "1a\\  \"builtin-baseline\": \"${BASELINE}\"," /tmp/mxl-manifest/vcpkg.json \
    && cmake -S /tmp/mxl -B /tmp/mxl/build -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE=/opt/vcpkg/scripts/buildsystems/vcpkg.cmake \
        -DVCPKG_MANIFEST_DIR=/tmp/mxl-manifest \
        -DCMAKE_BUILD_TYPE=Release \
        -DBUILD_TESTS=OFF -DBUILD_TOOLS=OFF -DBUILD_UTILS=OFF -DBUILD_DOCS=OFF \
        -DMXL_ENABLE_FABRICS_OFI=OFF \
        -DCMAKE_INSTALL_PREFIX=/opt/mxl \
    && cmake --build /tmp/mxl/build -j"$(nproc)" \
    && cmake --install /tmp/mxl/build \
    && cp -a /opt/mxl/lib/. /usr/local/lib/ \
    && cp -a /tmp/mxl/build/vcpkg_installed/x64-linux/lib/. /usr/local/lib/ \
    && ldconfig
WORKDIR /src
COPY . .
RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="/opt/mxl;/tmp/mxl/build/vcpkg_installed/x64-linux" \
    && cmake --build build -j"$(nproc)" \
    && cp build/mxl-avsync /usr/local/bin/mxl-avsync
EXPOSE 8180 3332
ENV AVSYNC_STATE_DIR=/config
ENTRYPOINT ["/usr/local/bin/mxl-avsync"]
CMD ["serve"]
