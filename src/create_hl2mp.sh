#!/bin/bash

set -euo pipefail

script=$(readlink -f -- "$0")

pushd "$(dirname -- "$script")" > /dev/null

source sdk_container

run_in_sniper "$@"

if [ $# -eq 0 ]; then
    export VPC_NINJA_BUILD_MODE="release"
else
    if [[ "$1" == "debug" ]]; then
        export VPC_NINJA_BUILD_MODE="debug"
    elif [[ "$1" == "release" ]]; then
        export VPC_NINJA_BUILD_MODE="release"
    else
        echo "Usage: $0 [debug|release]"
        exit 1
    fi
fi

solution_out="_vpc_/ninja/HL2MP_$VPC_NINJA_BUILD_MODE"

if [[ ! -e "$solution_out.ninja" ]]; then

    devtools/bin/vpc \
        /hl2mp \
        /linux64 \
        /ninja \
        /define:SOURCESDK \
        +game \
        /mksln "$solution_out"

    # Generate compile database
    ninja -f "$solution_out.ninja" -t compdb > compile_commands.json

    # Remove unsupported clang options
    sed -i \
        's/-fpredictive-commoning//g; s/-fvar-tracking-assignments//g' \
        compile_commands.json
fi

ninja -f "$solution_out.ninja" -j$(nproc)

popd
