// Copyright 2012 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

package main

import (
	"fmt"
	"os"
)

/*
 * Helpers for building runtime.
 */

// mkzversion writes zversion.go:
//
//	package runtime
//	const defaultGoroot = <goroot>
//	const theVersion = <version>
//
func mkzversion(dir, file string) {
	out := fmt.Sprintf(
		"// auto generated by go tool dist\n"+
			"\n"+
			"package runtime\n"+
			"\n"+
			"const defaultGoroot = `%s`\n"+
			"const theVersion = `%s`\n"+
			"const goexperiment = `%s`\n"+
			"var buildVersion = theVersion\n", goroot_final, findgoversion(), os.Getenv("GOEXPERIMENT"))

	writefile(out, file, 0)
}

// mkzbootstrap writes cmd/internal/obj/zbootstrap.go:
//
//	package obj
//
//	const defaultGOROOT = <goroot>
//	const defaultGO386 = <go386>
//	const defaultGOARM = <goarm>
//	const defaultGOOS = <goos>
//	const defaultGOARCH = <goarch>
//	const version = <version>
//	const goexperiment = <goexperiment>
//
func mkzbootstrap(file string) {
	out := fmt.Sprintf(
		"// auto generated by go tool dist\n"+
			"\n"+
			"package obj\n"+
			"\n"+
			"const defaultGOROOT = `%s`\n"+
			"const defaultGO386 = `%s`\n"+
			"const defaultGOARM = `%s`\n"+
			"const defaultGOOS = `%s`\n"+
			"const defaultGOARCH = `%s`\n"+
			"const version = `%s`\n"+
			"const goexperiment = `%s`\n",
		goroot_final, go386, goarm, gohostos, gohostarch, findgoversion(), os.Getenv("GOEXPERIMENT"))

	writefile(out, file, 0)
}
