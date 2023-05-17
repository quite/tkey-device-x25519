[![ci](https://github.com/tillitis/tkey-device-signer/actions/workflows/ci.yaml/badge.svg?branch=main&event=push)](https://github.com/tillitis/tkey-device-signer/actions/workflows/ci.yaml)

# Tillitis TKey Signer

The TKey `signer` device application is an ed25519 signing oracle. It
can sign messages up to 4 kByte. It is used by the
[tkey-ssh-agent](https://github.com/tillitis/tillitis-key1-apps) for
SSH authentication.

See [Release notes](RELEASE.md).

## Client Go package

We provide a Go package to use with `signer`:

- https://github.com/tillitis/tkeysign [![Go Reference](https://pkg.go.dev/badge/github.com/tillitis/tkeysign.svg)](https://pkg.go.dev/github.com/tillitis/tkeysign)

## Signer application protocol

`signer` has a simple protocol on top of the [TKey Framing
Protocol](https://dev.tillitis.se/protocol/#framing-protocol) with the
following requests:

| *command*             | *FP length* | *code* | *data*                              | *response*            |
|-----------------------|-------------|--------|-------------------------------------|-----------------------|
| `CMD_GET_PUBKEY`      | 1 B         | 0x01   | none                                | `RSP_GET_PUBKEY`      |
| `CMD_SET_SIZE`        | 32 B        | 0x03   | size as 32 bit LE                   | `RSP_SET_SIZE`        |
| `CMD_SIGN_DATA`       | 128 B       | 0x05   | 127 B null-padded data to be signed | `RSP_SIGN_DATA`       |
| `CMD_GET_SIG`         | 1 B         | 0x07   | none                                | `RSP_GET_SIG`         |
| `CMD_GET_NAMEVERSION` | 1 B         | 0x09   | none                                | `RSP_GET_NAMEVERSION` |

| *response*            | *FP length* | *code* | *data*                             |
|-----------------------|-------------|--------|------------------------------------|
| `RSP_GET_PUBKEY`      | 128 B       | 0x02   | 32 byte ed25519 public key         |
| `RSP_SET_SIZE`        | 4 B         | 0x04   | 1 byte status                      |
| `RSP_SIGN_DATA`       | 4 B         | 0x06   | 1 byte status                      |
| `RSP_GET_SIG`         | 128 B       | 0x08   | 64 byte signature                  |
| `RSP_GET_NAMEVERSION` | 32 B        | 0x0a   | 2 * 4 byte name, version 32 bit LE |
| `RSP_UNKNOWN_CMD`     | 1 B         | 0xff   | none                               |

| *status replies* | *code* |
|------------------|--------|
| OK               | 0      |
| BAD              | 1      |

It identifies itself with:

- `name0`: "tk1  "
- `name1`: "sign"

Please note that `signer` also replies with a `NOK` Framing Protocol
response status if the endpoint field in the FP header is meant for
the firmware (endpoint = `DST_FW`). This is recommended for
well-behaved device applications so the client side can probe for the
firmware.

Typical use by a client application:

1. Probe for firmware by sending firmware's `GET_NAME_VERSION` with FP
   header endpoint = `DST_FW`.
2. If firmware is found, load `signer`.
3. Upon receiving the device app digest back from firmware, switch to
   start talking the `signer` protocol above.
4. Send `CMD_GET_PUBKEY` to receive the `signer`'s public key. If the
   public key is already stored, check against it so it's the expected
   key.
5. Send `CMD_SET_SIZE` to set the size of the message to sign.
6. Send repeated messages with `CMD_SIGN_DATA` to send the entire
   message.
7. Send `CMD_GET_SIG` to get the signature over the message.

**Please note**: The firmware detection mechanism is not by any means
secure. If in doubt a user should always remove the TKey and insert it
again before doing any operation.

## Licenses and SPDX tags

Unless otherwise noted, the project sources are licensed under the
terms and conditions of the "GNU General Public License v2.0 only":

> Copyright Tillitis AB.
>
> These programs are free software: you can redistribute it and/or
> modify it under the terms of the GNU General Public License as
> published by the Free Software Foundation, version 2 only.
>
> These programs are distributed in the hope that it will be useful,
> but WITHOUT ANY WARRANTY; without even the implied warranty of
> MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
> General Public License for more details.

> You should have received a copy of the GNU General Public License
> along with this program. If not, see:
>
> https://www.gnu.org/licenses

See [LICENSE](LICENSE) for the full GPLv2-only license text.

External source code we have imported are isolated in their own
directories. They may be released under other licenses. This is noted
with a similar `LICENSE` file in every directory containing imported
sources.

The project uses single-line references to Unique License Identifiers
as defined by the Linux Foundation's [SPDX project](https://spdx.org/)
on its own source files, but not necessarily imported files. The line
in each individual source file identifies the license applicable to
that file.

The current set of valid, predefined SPDX identifiers can be found on
the SPDX License List at:

https://spdx.org/licenses/

## Building

You have two options for build tools: either you use our OCI image
`ghcr.io/tillitis/tkey-builder` or native tools.

In either case you need the device libraries in a directory next to
this one. The device libraries are available in:

https://github.com/tillitis/tkey-libs

Clone and build the device libraries first. You will most likely want
to specify a release with something like `-b v0.0.1`:

```
$ git clone -b v0.0.1 --depth 1 https://github.com/tillitis/tkey-libs
$ cd tkey-libs
$ make
```

### Building with Podman

We provide an OCI image with all tools you can use to build the
tkey-libs and the apps. If you have `make` and Podman installed you
can use it like this in the `tkey-libs` directory and then this
directory:

```
make podman
```

and everything should be built. This assumes a working rootless
podman. On Ubuntu 22.10, running

```
apt install podman rootlesskit slirp4netns
```

should be enough to get you a working Podman setup.

### Building with host tools

To build with native tools you need at least the `clang`, `llvm`,
`lld`, packages installed. Version 15 or later of LLVM/Clang is for
support of our architecture (RV32\_Zmmul). Ubuntu 22.10 (Kinetic) is
known to have this. Please see
[toolchain_setup.md](https://github.com/tillitis/tillitis-key1/blob/main/doc/toolchain_setup.md)
(in the tillitis-key1 repository) for detailed information on the
currently supported build and development environment.

Build everything:

```
$ make
```

If you cloned `tkey-libs` to somewhere else then the default set
`LIBDIR` to the path of the directory.

If your available `objcopy` is anything other than the default
`llvm-objcopy`, then define `OBJCOPY` to whatever they're called on
your system.

### Disabling touch requirement

The `signer` normally requires the TKey to be physically touched for
signing to complete. For special purposes it can be compiled with this
requirement removed by setting the environment variable
`TKEY_SIGNER_APP_NO_TOUCH` to some value when building. Example: 

```
$ make TKEY_SIGNER_APP_NO_TOUCH=yesplease
```

Of course this changes the signer app binary and as a consequence the
derived private key and identity will change.

## Running

Please see the [Developer
Handbook](https://dev.tillitis.se/tools/#qemu) for [how to run with
QEMU](https://dev.tillitis.se/tools/#qemu) or [how to run apps on a
TKey](https://dev.tillitis.se/devapp/#running-tkey-apps) but generally
to run `signer` you either use our
[tkey-ssh-agent](https://github.com/tillitis/tillitis-key1-apps) or
you use our development tool
[tkey-runapp](https://github.com/tillitis/tillitis-key1-apps) or the
script `runsign.sh` (also in the above repo) to run it manually.

```
$ ./runsign.sh file-with-message
```

```
$ ./tkey-runapp apps/signer/app.bin
$ ./tkey-sign file-with-message
```

Use `--port` if the device port is not automatically detected.

