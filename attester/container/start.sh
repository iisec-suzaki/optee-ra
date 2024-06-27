#!/bin/bash
set -euo pipefail

IMAGE_NAME="optee-attester"
TAG="latest"

SCRIPT_DIR=$(dirname "$(realpath "$0")")

if ! command -v docker &> /dev/null
then
    echo "Error: Docker is not installed. Please install Docker and try again."
    exit 1
fi

if ! docker info &> /dev/null
then
    echo "Error: Docker is not running. Please start Docker and try again."
    exit 1
fi

echo "Building Docker image..."
docker build -t ${IMAGE_NAME}:${TAG} \
             --build-arg USER_UID=$(id -u) \
             --build-arg USER_GID=$(id -g) \
             -f "${SCRIPT_DIR}/Dockerfile" "${SCRIPT_DIR}/.."

echo "Docker image ${IMAGE_NAME}:${TAG} has been successfully built."

echo "Running Docker container..."
docker run --rm -it \
           --entrypoint=bash \
           -v "${SCRIPT_DIR}/../remote_attestation:/optee/optee_examples/remote_attestation" \
           -v "${SCRIPT_DIR}/../pta_remote_attestation/remote_attestation:/optee/optee_os/core/pta/remote_attestation" \
           -v "${SCRIPT_DIR}/../pta_remote_attestation/pta_remote_attestation.h:/optee/optee_os/lib/libutee/include/pta_remote_attestation.h" \
           --network veraison-net \
           ${IMAGE_NAME}:${TAG}

echo "Docker container has been successfully run."
