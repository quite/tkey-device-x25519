
This TKey device app implements X25519, the ECDH key agreement
protocol for Curve25519. X25519 is implemented using
[Monocypher](https://monocypher.org/manual/x25519).

It is used by
[age-plugin-tkey](https://github.com/quite/age-plugin-tkey) which uses
the Go package [tkeyx25519](https://github.com/quite/tkeyx25519) to
communicate with this device app running on the TKey. But this can of
course also be used by other parties wanting to do ECDH.

Note that this is work in progress. The implementation may change, and
this will cause a change of identity of a TKey running this device
app. This would mean that the public/private key no longer is the
same, and decryption of data encrypted for the previous key pair will
not be possible.

Based on https://github.com/tillitis/tkey-device-signer

# Building

You can build the device app locally by running [build.sh](build.sh),
or checking out what it does.

For reproducibility the device app is typically built in a container,
locking down the toolchain, and using specific versions of
dependencies. Because if one single bit changes in the app.bin that
will run on the TKey (for example due to a newer clang/llvm), then the
identity (private/public key) of it will change.

You can use [build-in-container.sh](build-in-container.sh) to do this
using our own container image (see
[Containerfile](https://github.com/quite/age-plugin-tkey/blob/main/Containerfile)
in the age-plugin-tkey repo). The clone of this repo that you're
sitting in will be mounted into the container and built, but
dependencies will be freshly cloned as they don't exist inside (it
runs `build.sh` there). `podman` is used for running the container
(packages: `podman rootlesskit slirp4netns`).

The `x25519/app.bin.sha512` contains the expected hash of the device
app binary when built using our container image which currently has
clang 17.
