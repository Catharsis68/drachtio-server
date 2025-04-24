# Stage 1: Build environment
FROM debian:bookworm-slim AS builder

ARG BUILD_CPUS=16
ARG DETECTED_TAG=v0.8.27-rc17-tmp-2

# Install build dependencies
RUN set -ex \
    && apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get -y upgrade \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        # Build tools
        autoconf \
        automake \
        build-essential \
        cmake \
        g++ \
        gcc \
        git \
        libtool \
        libtool-bin \
        make \
        pkg-config \
        python3 \
        autoconf-archive \
        # Required libraries for building
        ca-certificates \
        libcurl4-openssl-dev \
        libgoogle-perftools-dev \
        libssl-dev \
        zlib1g-dev

RUN mkdir -p /usr/local/src/drachtio-server

# Copy local source code if building locally
COPY . /usr/local/src/drachtio-server-local/

# Use a script to handle the build logic
RUN set -ex \
    && git clone --depth=1 --branch ${DETECTED_TAG} --single-branch \
    https://github.com/Catharsis68/drachtio-server /usr/local/src/drachtio-server \
    && cd /usr/local/src/drachtio-server \
    && git verify-tag ${DETECTED_TAG} || true \
    && git submodule update --init --recursive --depth=1 \
    && ./bootstrap.sh \
    && rm -rf build \
    && mkdir build \
    && cd build \
    && ../configure --enable-tcmalloc=yes \
       CPPFLAGS='-DNDEBUG' \
       CXXFLAGS='-O2 -Wall -Wextra' \
    && make -j${BUILD_CPUS} \
    && make install

# Stage 2: Runtime environment
FROM debian:bookworm-slim

ARG DETECTED_TAG
LABEL version="${DETECTED_TAG}"

# Install runtime dependencies only
RUN set -ex \
    && apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        curl \
        jq \
        libcurl4 \
        libgoogle-perftools4 \
        libssl3 \
        zlib1g \
    && rm -rf \
        /var/lib/apt/* \
        /var/lib/dpkg/* \
        /var/lib/cache/* \
        /var/log/* \
        /var/lib/apt/lists/*

# Copy built artifacts from builder stage
COPY --from=builder /usr/local/bin/drachtio /usr/local/bin/
COPY --from=builder /usr/local/src/drachtio-server/docker.drachtio.conf.xml /etc/drachtio.conf.xml
COPY --chmod=755 ./entrypoint.sh /

VOLUME ["/config"]

ENTRYPOINT ["/entrypoint.sh"]
CMD ["drachtio"]
