#!/bin/bash
set -euo pipefail

IMAGE_NAME="optee-attester"
TAG="latest"
EXPORT_PATH="../distribution/${IMAGE_NAME}.tar"

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
             -f Dockerfile ..

echo "Exporting Docker image to ${EXPORT_PATH}..."
docker save -o ${EXPORT_PATH} ${IMAGE_NAME}:${TAG}

echo "Docker image has been successfully saved to ${EXPORT_PATH}"
echo "You can now distribute ${EXPORT_PATH} as a binary."
