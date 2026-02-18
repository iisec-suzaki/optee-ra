#!/bin/bash
set -euo pipefail

THIS_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
VERAISON=${THIS_DIR}/../services/deployments/docker/veraison

source ${THIS_DIR}/../services/deployments/docker/env.bash

# Select environment: qemu (default) or imx
ENV=${1:-qemu}
REFVAL_FILE="${THIS_DIR}/data/comid-psa-refval-${ENV}.json"

if [[ ! -f "$REFVAL_FILE" ]]; then
    echo "Error: Unknown environment '$ENV'. Use 'qemu' or 'imx'."
    exit 1
fi

echo "Using provisioning data for: $ENV"

build_endorsements() {
    ${VERAISON} -- cocli comid create \
        --template ${THIS_DIR}/data/comid-psa-ta.json \
        --template ${REFVAL_FILE} \
        --output-dir ${THIS_DIR}/data
    ${VERAISON} -- cocli corim create \
        --template ${THIS_DIR}/data/corim-psa.json \
        --comid ${THIS_DIR}/data/comid-psa-refval-${ENV}.cbor \
        --comid ${THIS_DIR}/data/comid-psa-ta.cbor \
        --output ${THIS_DIR}/data/psa-endorsements.cbor
}

submit_endorsements() {
    $VERAISON -- cocli corim submit \
        --corim-file "${THIS_DIR}/data/psa-endorsements.cbor" \
        --api-server "https://provisioning-service:${PROVISIONING_PORT}/endorsement-provisioning/v1/submit" \
        --media-type 'application/corim-unsigned+cbor; profile="http://arm.com/psa/iot/1"' \
        --insecure
}

build_endorsements
submit_endorsements
