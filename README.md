
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

You can build the device app locally by first building the tkey-libs
dependency in a sibling directory, like this:

```
git -C .. clone https://github.com/tillitis/tkey-libs
git -C ../tkey-libs checkout v0.0.2
make -C ../tkey-libs -j
make -j
```

For reproducability we typically build the device app using a
container image, thus locking down the toolchain. Because if one
single bit changes in the app.bin that will run on the TKey (for
example due to a newer llvm), then the identity (private/public key)
of it will change. `x25519/app.bin.sha512` contains the currently
expected hash of the device app binary.

There are some
[tools](https://github.com/quite/age-plugin-tkey/tree/main/contrib) in
the age-plugin-tkey repository for doing that.
