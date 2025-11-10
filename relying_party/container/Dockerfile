FROM ubuntu:24.04
ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get -y install --no-install-suggests --no-install-recommends \
        build-essential \
        ca-certificates \
        curl \
        git \
        golang-go \
        libssl-dev \
        make \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

# Use the default ubuntu user that exists in Ubuntu 24.04 (UID 1000)
ARG USERNAME=ubuntu
ARG USER_UID=1000
ARG USER_GID=${USER_UID}

# Create app directory
ENV APP_DIR=/app
RUN mkdir ${APP_DIR} \
    && chown ${USERNAME}:${USERNAME} ${APP_DIR}

# Install a tool to create, verify and display EAR attestation results
USER ${USERNAME}:${USERNAME}
RUN go install github.com/veraison/ear/arc@latest
ENV PATH="/home/${USERNAME}/go/bin:${PATH}"

# Install user's tools
USER root:root
RUN apt-get update \
    && apt-get -y install --no-install-suggests --no-install-recommends \
        clang-format \
        nano \
        net-tools \
    && rm -rf /var/lib/apt/lists/*

USER ${USERNAME}:${USERNAME}
WORKDIR ${APP_DIR}
ENTRYPOINT make && ./rp
