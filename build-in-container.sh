#!/bin/bash

builderimage=${1:-ghcr.io/quite/tkey-apps-builder:1}
printf "$0 using builderimage: %s\n" "$builderimage"

scriptf="$(mktemp)"

cat >"$scriptf" <<EOF
#!/bin/bash
set -eu

# remove what we'll produce (if successful)
rm -f /hostrepo/x25519/app.bin

mkdir /src
cp -af /hostrepo /src/tkey-device-x25519

cd /src/tkey-device-x25519
./build.sh

cp -afv ./x25519/app.bin /hostrepo/x25519/app.bin
EOF

chmod +x "$scriptf"

podman run --rm -i -t \
 --mount "type=bind,source=$(git rev-parse --show-toplevel),destination=/hostrepo" \
 --mount "type=bind,source=$scriptf,destination=/script.sh" \
 "$builderimage" \
 /script.sh

rm -f "$scriptf"
