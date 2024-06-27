FROM ubuntu:22.04
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get -y install --no-install-suggests --no-install-recommends \
        adb \
        acpica-tools \
        autoconf \
        automake \
        bc \
        bison \
        build-essential \
        ca-certificates \
        ccache \
        cpio \
        cscope \
        curl \
        device-tree-compiler \
        e2tools \
        expect \
        fastboot \
        flex \
        ftp-upload \
        gdisk \
        git \
        gpg-agent \
        libattr1-dev \
        libcap-dev \
        libfdt-dev \
        libftdi-dev \
        libglib2.0-dev \
        libgmp3-dev \
        libhidapi-dev \
        libmpc-dev \
        libncurses5-dev \
        libpixman-1-dev \
        libslirp-dev \
        libssl-dev \
        libtool \
        libusb-1.0-0-dev \
        make \
        mtools \
        netcat \
        ninja-build \
        openssh-client \
        python3-cryptography \
        python3-pip \
        python3-pyelftools \
        python3-serial \
        python-is-python3 \
        repo \
        rsync \
        swig \
        unzip \
        uuid-dev \
        wget \
        xdg-utils \
        xterm \
        xz-utils \
        zlib1g-dev \
    && rm -rf /var/lib/apt/lists/*

# Create an user
ARG USERNAME=user
ARG USER_UID=1000
ARG USER_GID=${USER_UID}
RUN groupadd -o --gid ${USER_GID} ${USERNAME} \
    && useradd --uid ${USER_UID} --gid ${USER_GID} -m ${USERNAME}

# Install OP-TEE
ENV OPTEE_DIR /optee
ENV OPTEE_VERSION 3.21.0
RUN mkdir ${OPTEE_DIR} \
    && chown ${USERNAME}:${USERNAME} ${OPTEE_DIR}
USER ${USERNAME}:${USERNAME}
RUN cd ${OPTEE_DIR} \
    && repo init -u https://github.com/OP-TEE/manifest.git \
        -m qemu_v8.xml -b ${OPTEE_VERSION} \
    && repo sync -j$(nproc) \
    && cd ${OPTEE_DIR}/build \
    && make toolchains -j$(nproc) \
    && make check -j$(nproc)

# Install rust to build rust-apiclient
USER ${USERNAME}:${USERNAME}
RUN curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh -s -- -y
ENV PATH="${PATH}:/home/${USERNAME}/.cargo/bin"
USER root:root

# Install dependencies for cross-compilation
RUN apt-get update \
    && apt-get -y install --no-install-suggests --no-install-recommends \
        libc6-dev-arm64-cross linux-libc-dev-arm64-cross g++-aarch64-linux-gnu \
    && rm -rf /var/lib/apt/lists/*

# Install openssl to build rust-apiclient
RUN wget https://www.openssl.org/source/openssl-3.1.1.tar.gz \
    && tar -xvf openssl-3.1.1.tar.gz \
    && cd openssl-3.1.1 \
    && ./Configure linux-aarch64 --cross-compile-prefix=/usr/bin/aarch64-linux-gnu- \
        --prefix=/opt/openssl-3.1.1 --openssldir=/opt/openssl-3.1.1 -static -g -O2 -gdwarf-4 \
    && make -j$(nproc) \
    && make install

# Build rust-apiclient for aarch64
ENV RUST_APICLIENT_DIR /rust-apiclient
RUN mkdir ${RUST_APICLIENT_DIR} \
    && chown ${USERNAME}:${USERNAME} ${RUST_APICLIENT_DIR}
USER ${USERNAME}:${USERNAME}
WORKDIR ${RUST_APICLIENT_DIR}
RUN git clone https://github.com/veraison/rust-apiclient . \
    && git checkout e34784d \
    && rustup target add aarch64-unknown-linux-gnu \
    && OPENSSL_DIR=/opt/openssl-3.1.1/ cargo build --target aarch64-unknown-linux-gnu --release

# Install user's tools
USER root:root
RUN apt-get update \
    && apt-get -y install --no-install-suggests --no-install-recommends \
        clang-format \
        nano \
    && rm -rf /var/lib/apt/lists/*

WORKDIR ${OPTEE_DIR}
USER ${USERNAME}:${USERNAME}
