# syntax=docker/dockerfile:1
# Pins: IMPLEMENTATION_PLAN.md. MXL_REF is the single MXL pin (also the MXL_REF env in .github/workflows/ci.yaml).
# No cap_sys_nice: the writers sleep on TAI with mxlSleepUntil and do not use real-time scheduling.

FROM ubuntu:24.04 AS build
ARG MXL_REF=218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7
ARG NMOS_CPP_REF=fe303849527394b03bdedc8f161f377fe458bb62

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build git pkg-config curl zip unzip tar ca-certificates python3 \
        bison flex autoconf automake libtool \
        libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
        libjpeg-dev \
        libboost-system-dev libboost-thread-dev libboost-chrono-dev \
        libboost-date-time-dev libboost-regex-dev libboost-random-dev \
        libboost-filesystem-dev libcpprest-dev nlohmann-json3-dev \
        libssl-dev uuid-dev libavahi-compat-libdnssd-dev \
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
    && cmake --install /tmp/mxl/build

RUN git init /opt/nmos-cpp \
    && git -C /opt/nmos-cpp remote add origin https://github.com/sony/nmos-cpp.git \
    && git -C /opt/nmos-cpp fetch --depth 1 origin "${NMOS_CPP_REF}" \
    && git -C /opt/nmos-cpp checkout FETCH_HEAD

COPY . /src
RUN cmake -S /src -B /src/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="/opt/mxl;/tmp/mxl/build/vcpkg_installed/x64-linux" \
        -DAVSYNC_WITH_NMOS=ON -DNMOS_CPP_DIR=/opt/nmos-cpp/Development \
    && cmake --build /src/build -j"$(nproc)" \
    && /src/build/avsync-core-tests \
    && /src/build/avsync-picture-tests

FROM ubuntu:24.04 AS runtime
ARG MXL_REF=218ddaa0a08c12ffe75fc475ae65aa3d9eef16d7
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        ca-certificates curl \
        libavcodec60 libavformat60 libavutil58 libswscale7 libswresample4 \
        libjpeg-turbo8 \
        libcpprest2.10 \
        libboost-chrono1.83.0t64 libboost-thread1.83.0 \
        libboost-filesystem1.83.0 libboost-regex1.83.0 libboost-date-time1.83.0 \
        libssl3t64 libavahi-compat-libdnssd1 \
    && rm -rf /var/lib/apt/lists/*
COPY --from=build /opt/mxl/lib/ /usr/local/lib/
COPY --from=build /tmp/mxl/build/vcpkg_installed/x64-linux/lib/ /usr/local/lib/
COPY --from=build /src/build/mxl-avsync /usr/local/bin/mxl-avsync
COPY web /usr/local/share/mxl-avsync/web
COPY --from=build /src/web/icon.png /src/web/icon.png
COPY LICENSE NOTICE /usr/share/doc/mxl-avsync/
LABEL org.opencontainers.image.title="mxl-avsync" \
      org.opencontainers.image.source="https://github.com/LeeO86/mxl-avsync" \
      io.dmf.mxl.revision="${MXL_REF}"
RUN ldconfig \
    && if ! getent passwd 1000 >/dev/null; then \
         useradd --uid 1000 --no-create-home --shell /usr/sbin/nologin mxl; \
       fi \
    && mkdir -p /config && chown 1000:1000 /config
USER 1000:1000
ENV AVSYNC_STATE_DIR=/config \
    AVSYNC_WEB_ROOT=/usr/local/share/mxl-avsync/web
EXPOSE 8180 3332
ENTRYPOINT ["/usr/local/bin/mxl-avsync"]
CMD ["serve"]
