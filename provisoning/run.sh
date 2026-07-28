#!/bin/bash
set -euo pipefail

THIS_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
VERAISON=${THIS_DIR}/../services/deployments/docker/veraison

source ${THIS_DIR}/../services/deployments/docker/env.bash

# Select environment: qemu (default), imx, or a per-build variant such as
# imx-caam / imx-cpu (any name with a data/comid-psa-{refval,ta}-<env>.json pair)
ENV=${1:-qemu}
REFVAL_FILE="${THIS_DIR}/data/comid-psa-refval-${ENV}.json"
TA_FILE="${THIS_DIR}/data/comid-psa-ta-${ENV}.json"

if [[ ! -f "$REFVAL_FILE" ]] || [[ ! -f "$TA_FILE" ]]; then
    echo "Error: Unknown environment '$ENV'. Available:"
    ls "${THIS_DIR}"/data/comid-psa-refval-*.json | sed 's/.*comid-psa-refval-\(.*\)\.json/  \1/'
    exit 1
fi

echo "Using provisioning data for: $ENV"

build_endorsements() {
    ${VERAISON} -- cocli comid create \
        --template ${TA_FILE} \
        --template ${REFVAL_FILE} \
        --output-dir ${THIS_DIR}/data
    ${VERAISON} -- cocli corim create \
        --template ${THIS_DIR}/data/corim-psa.json \
        --comid ${THIS_DIR}/data/comid-psa-refval-${ENV}.cbor \
        --comid ${THIS_DIR}/data/comid-psa-ta-${ENV}.cbor \
        --output ${THIS_DIR}/data/psa-endorsements.cbor
}

submit_endorsements() {
    $VERAISON -- cocli corim submit \
        --corim-file "${THIS_DIR}/data/psa-endorsements.cbor" \
        --api-server "https://provisioning-service:8888/endorsement-provisioning/v1/submit" \
        --media-type 'application/corim-unsigned+cbor; profile="http://arm.com/psa/iot/1"' \
        --ca-cert /tmp/veraison/certs/rootCA.crt
}

build_endorsements
submit_endorsements
