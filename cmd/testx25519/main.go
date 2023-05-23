// Copyright (C) 2022, 2023 - Tillitis AB
// SPDX-License-Identifier: GPL-2.0-only

package main

import (
	"bytes"
	"crypto/ecdh"
	"crypto/rand"
	_ "embed"
	"errors"
	"fmt"
	"io"
	"log"
	"os"
	"os/signal"
	"syscall"

	"github.com/quite/tkey-device-x25519/internal/util"
	"github.com/quite/tkeyx25519"
	"github.com/spf13/pflag"
	"github.com/tillitis/tkeyclient"
)

// nolint:typecheck // Avoid lint error when the embedding file is missing.
// Makefile copies the built app here ./app.bin
//
//go:embed app.bin
var appBinary []byte

const (
	wantFWName0  = "tk1 "
	wantFWName1  = "mkdf"
	wantAppName0 = "x255"
	wantAppName1 = "19  "
)

var le = log.New(os.Stderr, "", 0)

func main() {
	var devPath string
	var speed int
	var helpOnly bool
	pflag.CommandLine.SortFlags = false
	pflag.StringVar(&devPath, "port", "",
		"Set serial port device `PATH`. If this is not passed, auto-detection will be attempted.")
	pflag.IntVar(&speed, "speed", tkeyclient.SerialSpeed,
		"Set serial port speed in `BPS` (bits per second).")
	pflag.BoolVar(&helpOnly, "help", false, "Output this help.")
	pflag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage:\n%s", pflag.CommandLine.FlagUsagesWrapped(80))
	}
	pflag.Parse()

	if helpOnly {
		pflag.Usage()
		os.Exit(0)
	}

	if devPath == "" {
		var err error
		devPath, err = util.DetectSerialPort(true)
		if err != nil {
			os.Exit(1)
		}
	}

	tkeyclient.SilenceLogging()

	tk := tkeyclient.New()
	le.Printf("Connecting to device on serial port %s...\n", devPath)
	if err := tk.Connect(devPath, tkeyclient.WithSpeed(speed)); err != nil {
		le.Printf("Could not open %s: %v\n", devPath, err)
		os.Exit(1)
	}

	tkeyX25519 := tkeyx25519.New(tk)

	exit := func(code int) {
		if err := tkeyX25519.Close(); err != nil {
			le.Printf("%v\n", err)
		}
		os.Exit(code)
	}
	handleSignals(func() { exit(1) }, os.Interrupt, syscall.SIGTERM)

	if isFirmwareMode(tk) {
		le.Printf("Device is in firmware mode. Loading app...\n")
		if err := tk.LoadApp(appBinary, []byte{}); err != nil {
			le.Printf("LoadApp failed: %v", err)
			exit(1)
		}
	}

	if !isWantedApp(tkeyX25519) {
		fmt.Printf("The TKey may already be running an app, but not the expected x25519-app.\n" +
			"Please unplug and plug it in again.\n")
		exit(1)
	}

	goX25519 := ecdh.X25519()

	hostPriv, err := goX25519.GenerateKey(rand.Reader)
	panicErr(err)
	hostPub := hostPriv.PublicKey()
	fmt.Printf("hostPub: %0x\n", hostPub.Bytes())

	var domain [78]byte
	var userSecret [16]byte
	// TODO these
	copy(domain[:], []byte("age..."))
	_, err = rand.Read(userSecret[:])
	panicErr(err)

	requireTouch := false

	tkeyPubBytes, err := tkeyX25519.GetPubKey(domain, userSecret, requireTouch)
	panicErr(err)
	tkeyPub, err := goX25519.NewPublicKey(tkeyPubBytes)
	panicErr(err)
	fmt.Printf("tkeyPub: %0x\n", tkeyPub.Bytes())

	hostShared, err := hostPriv.ECDH(tkeyPub)
	panicErr(err)
	fmt.Printf("hostShared: %0x\n", hostShared)

	tkeyShared, err := tkeyX25519.ComputeShared(domain, userSecret, requireTouch, [32]byte(hostPub.Bytes()))
	panicErr(err)
	fmt.Printf("tkeyShared: %0x\n", tkeyShared)

	if !bytes.Equal(hostShared, tkeyShared) {
		fmt.Printf("👎\n")
		exit(1)
	}

	fmt.Printf("👍\n")
	exit(0)
}

func panicErr(err error) {
	if err != nil {
		panic(err)
	}
}

func handleSignals(action func(), sig ...os.Signal) {
	ch := make(chan os.Signal, 1)
	signal.Notify(ch, sig...)
	go func() {
		for {
			<-ch
			action()
		}
	}()
}

func isFirmwareMode(tk *tkeyclient.TillitisKey) bool {
	nameVer, err := tk.GetNameVersion()
	if err != nil {
		if !errors.Is(err, io.EOF) && !errors.Is(err, tkeyclient.ErrResponseStatusNotOK) {
			le.Printf("GetNameVersion failed: %s\n", err)
		}
		return false
	}
	// not caring about nameVer.Version
	return nameVer.Name0 == wantFWName0 &&
		nameVer.Name1 == wantFWName1
}

func isWantedApp(x25519 tkeyx25519.X25519) bool {
	nameVer, err := x25519.GetAppNameVersion()
	if err != nil {
		if !errors.Is(err, io.EOF) {
			le.Printf("GetAppNameVersion: %s\n", err)
		}
		return false
	}
	// not caring about nameVer.Version
	return nameVer.Name0 == wantAppName0 &&
		nameVer.Name1 == wantAppName1
}
