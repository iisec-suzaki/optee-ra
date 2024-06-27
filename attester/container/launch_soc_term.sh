#!/bin/bash
set -euo pipefail

IMAGE_NAME="optee-attester"
TAG="latest"

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

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 [normal|secure]"
    exit 1
fi
MODE=$1

CONTAINER_ID=$(docker ps -q -f "ancestor=${IMAGE_NAME}:${TAG}" -n 1)
if [ -z "${CONTAINER_ID}" ]; then
    echo "Error: No running container found for ${IMAGE_NAME}:${TAG}."
    exit 1
fi

if [ "${MODE}" == "normal" ]; then
    echo "Accessing normal world terminal..."
    docker exec -it $CONTAINER_ID /optee/build/soc_term.py 54320
elif [ "${MODE}" == "secure" ]; then
    echo "Accessing secure world terminal..."
    docker exec -it $CONTAINER_ID /optee/build/soc_term.py 54321
else
    echo "Invalid argument: ${MODE}. Please use 'normal' or 'secure'."
    exit 1
fi
