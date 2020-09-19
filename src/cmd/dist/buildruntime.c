// Copyright 2012 The Go Authors.  All rights reserved.
// Use of this source code is governed by a BSD-style
// license that can be found in the LICENSE file.

#include "a.h"

/*
 * Helpers for building runtime.
 */

// mkzversion writes zversion.go:
//
//	package runtime
//	const defaultGoroot = <goroot>
//	const theVersion = <version>
//
void
mkzversion(char *dir, char *file)
{
	Buf b, out;
	
	USED(dir);

	binit(&b);
	binit(&out);
	
	bwritestr(&out, bprintf(&b,
		"// auto generated by go tool dist\n"
		"\n"
		"package runtime\n"
		"\n"
		"const defaultGoroot = `%s`\n"
		"const theVersion = `%s`\n"
		"var buildVersion = theVersion\n", goroot_final, goversion));

	writefile(&out, file, 0);
	
	bfree(&b);
	bfree(&out);
}

// mkzexperiment writes zaexperiment.h (sic):
//
//	#define GOEXPERIMENT "experiment string"
//
void
mkzexperiment(char *dir, char *file)
{
	Buf b, out, exp;
	
	USED(dir);

	binit(&b);
	binit(&out);
	binit(&exp);
	
	xgetenv(&exp, "GOEXPERIMENT");
	bwritestr(&out, bprintf(&b,
		"// auto generated by go tool dist\n"
		"\n"
		"#define GOEXPERIMENT \"%s\"\n", bstr(&exp)));

	writefile(&out, file, 0);
	
	bfree(&b);
	bfree(&out);
	bfree(&exp);
}

// mkzgoarch writes zgoarch_$GOARCH.go:
//
//	package runtime
//	const theGoarch = <goarch>
//
void
mkzgoarch(char *dir, char *file)
{
	Buf b, out;

	USED(dir);
	
	binit(&b);
	binit(&out);
	
	bwritestr(&out, bprintf(&b,
		"// auto generated by go tool dist\n"
		"\n"
		"package runtime\n"
		"\n"
		"const theGoarch = `%s`\n", goarch));

	writefile(&out, file, 0);
	
	bfree(&b);
	bfree(&out);
}

// mkzgoos writes zgoos_$GOOS.go:
//
//	package runtime
//	const theGoos = <goos>
//
void
mkzgoos(char *dir, char *file)
{
	Buf b, out;

	USED(dir);
	
	binit(&b);
	binit(&out);

	bwritestr(&out, "// auto generated by go tool dist\n\n");

	if(streq(goos, "linux")) {
		bwritestr(&out, "// +build !android\n\n");
	}
	
	bwritestr(&out, bprintf(&b,
		"package runtime\n"
		"\n"
		"const theGoos = `%s`\n", goos));

	writefile(&out, file, 0);
	
	bfree(&b);
	bfree(&out);
}

static struct {
	char *goarch;
	char *goos;
	char *hdr;
} zasmhdr[] = {
	{"386", "",
		"#define	get_tls(r)	MOVL TLS, r\n"
		"#define	g(r)	0(r)(TLS*1)\n"
	},
	{"amd64p32", "",
		"#define	get_tls(r)	MOVL TLS, r\n"
		"#define	g(r)	0(r)(TLS*1)\n"
	},
	{"amd64", "",
		"#define	get_tls(r)	MOVQ TLS, r\n"
		"#define	g(r)	0(r)(TLS*1)\n"
	},	

	{"arm", "",
	"#define	LR	R14\n"
	},

	{"power64", "",
	"#define	g	R30\n"
	},
	{"power64le", "",
	"#define	g	R30\n"
	},
};

#define MAXWINCB 2000 /* maximum number of windows callbacks allowed */

// mkzasm writes zasm_$GOOS_$GOARCH.h,
// which contains struct offsets for use by
// assembly files.  It also writes a copy to the work space
// under the name zasm_GOOS_GOARCH.h (no expansion).
// 
void
mkzasm(char *dir, char *file)
{
	int i, n;
	char *aggr, *p;
	Buf in, b, b1, out, exp;
	Vec argv, lines, fields;

	binit(&in);
	binit(&b);
	binit(&b1);
	binit(&out);
	binit(&exp);
	vinit(&argv);
	vinit(&lines);
	vinit(&fields);
	
	bwritestr(&out, "// auto generated by go tool dist\n\n");
	if(streq(goos, "linux")) {
		bwritestr(&out, "// +build !android\n\n");
	}
	
	for(i=0; i<nelem(zasmhdr); i++) {
		if(hasprefix(goarch, zasmhdr[i].goarch) && hasprefix(goos, zasmhdr[i].goos)) {
			bwritestr(&out, zasmhdr[i].hdr);
			goto ok;
		}
	}
	fatal("unknown $GOOS/$GOARCH in mkzasm");
ok:

	copyfile(bpathf(&b, "%s/pkg/%s_%s/textflag.h", goroot, goos, goarch),
		bpathf(&b1, "%s/src/cmd/ld/textflag.h", goroot), 0);

	// Run 6c -D GOOS_goos -D GOARCH_goarch -I workdir -a -n -o workdir/proc.acid proc.c
	// to get acid [sic] output. Run once without the -a -o workdir/proc.acid in order to
	// report compilation failures (the -o redirects all messages, unfortunately).
	vreset(&argv);
	vadd(&argv, bpathf(&b, "%s/%sc", tooldir, gochar));
	vadd(&argv, "-D");
	vadd(&argv, bprintf(&b, "GOOS_%s", goos));
	vadd(&argv, "-D");
	vadd(&argv, bprintf(&b, "GOARCH_%s", goarch));
	vadd(&argv, "-I");
	vadd(&argv, bprintf(&b, "%s", workdir));
	vadd(&argv, "-I");
	vadd(&argv, bprintf(&b, "%s/pkg/%s_%s", goroot, goos, goarch));
	vadd(&argv, "-n");
	vadd(&argv, "-a");
	vadd(&argv, "-o");
	vadd(&argv, bpathf(&b, "%s/proc.acid", workdir));
	vadd(&argv, "proc.c");
	runv(nil, dir, CheckExit, &argv);
	readfile(&in, bpathf(&b, "%s/proc.acid", workdir));
	
	// Convert input like
	//	aggr G
	//	{
	//		Gobuf 24 sched;
	//		'Y' 48 stack0;
	//	}
	//	StackMin = 128;
	// into output like
	//	#define g_sched 24
	//	#define g_stack0 48
	//	#define const_StackMin 128
	aggr = nil;
	splitlines(&lines, bstr(&in));
	for(i=0; i<lines.len; i++) {
		splitfields(&fields, lines.p[i]);
		if(fields.len == 2 && streq(fields.p[0], "aggr")) {
			if(streq(fields.p[1], "G"))
				aggr = "g";
			else if(streq(fields.p[1], "M"))
				aggr = "m";
			else if(streq(fields.p[1], "P"))
				aggr = "p";
			else if(streq(fields.p[1], "Gobuf"))
				aggr = "gobuf";
			else if(streq(fields.p[1], "LibCall"))
				aggr = "libcall";
			else if(streq(fields.p[1], "WinCallbackContext"))
				aggr = "cbctxt";
			else if(streq(fields.p[1], "SEH"))
				aggr = "seh";
			else if(streq(fields.p[1], "Alg"))
				aggr = "alg";
			else if(streq(fields.p[1], "Panic"))
				aggr = "panic";
			else if(streq(fields.p[1], "Stack"))
				aggr = "stack";
		}
		if(hasprefix(lines.p[i], "}"))
			aggr = nil;
		if(aggr && hasprefix(lines.p[i], "\t") && fields.len >= 2) {
			n = fields.len;
			p = fields.p[n-1];
			if(p[xstrlen(p)-1] == ';')
				p[xstrlen(p)-1] = '\0';
			bwritestr(&out, bprintf(&b, "#define %s_%s %s\n", aggr, fields.p[n-1], fields.p[n-2]));
		}
		if(fields.len == 3 && streq(fields.p[1], "=")) { // generated from enumerated constants
			p = fields.p[2];
			if(p[xstrlen(p)-1] == ';')
				p[xstrlen(p)-1] = '\0';
			bwritestr(&out, bprintf(&b, "#define const_%s %s\n", fields.p[0], p));
		}
	}

	// Some #defines that are used for .c files.
	if(streq(goos, "windows")) {
		bwritestr(&out, bprintf(&b, "#define cb_max %d\n", MAXWINCB));
	}
	
	xgetenv(&exp, "GOEXPERIMENT");
	bwritestr(&out, bprintf(&b, "#define GOEXPERIMENT \"%s\"\n", bstr(&exp)));
	
	// Write both to file and to workdir/zasm_GOOS_GOARCH.h.
	writefile(&out, file, 0);
	writefile(&out, bprintf(&b, "%s/zasm_GOOS_GOARCH.h", workdir), 0);

	bfree(&in);
	bfree(&b);
	bfree(&b1);
	bfree(&out);
	bfree(&exp);
	vfree(&argv);
	vfree(&lines);
	vfree(&fields);
}

// mkzsys writes zsys_$GOOS_$GOARCH.s,
// which contains arch or os specific asm code.
// 
void
mkzsys(char *dir, char *file)
{
	int i;
	Buf out;

	USED(dir);
	
	binit(&out);
	
	bwritestr(&out, "// auto generated by go tool dist\n\n");
	if(streq(goos, "linux")) {
		bwritestr(&out, "// +build !android\n\n");
	}
	
	if(streq(goos, "windows")) {
		bwritef(&out,
			"// runtime·callbackasm is called by external code to\n"
			"// execute Go implemented callback function. It is not\n"
			"// called from the start, instead runtime·compilecallback\n"
			"// always returns address into runtime·callbackasm offset\n"
			"// appropriately so different callbacks start with different\n"
			"// CALL instruction in runtime·callbackasm. This determines\n"
			"// which Go callback function is executed later on.\n"
			"TEXT runtime·callbackasm(SB),7,$0\n");
		for(i=0; i<MAXWINCB; i++) {
			bwritef(&out, "\tCALL\truntime·callbackasm1(SB)\n");
		}
		bwritef(&out, "\tRET\n");
	}

	writefile(&out, file, 0);
	
	bfree(&out);
}

static char *runtimedefs[] = {
	"defs.c",
	"malloc.c",
	"mcache.c",
	"mgc0.c",
	"proc.c",
	"parfor.c",
	"stack.c",
};

// mkzruntimedefs writes zruntime_defs_$GOOS_$GOARCH.h,
// which contains Go struct definitions equivalent to the C ones.
// Mostly we just write the output of 6c -q to the file.
// However, we run it on multiple files, so we have to delete
// the duplicated definitions, and we don't care about the funcs,
// so we delete those too.
// 
void
mkzruntimedefs(char *dir, char *file)
{
	int i, skip;
	char *p;
	Buf in, b, b1, out;
	Vec argv, lines, fields, seen;
	
	binit(&in);
	binit(&b);
	binit(&b1);
	binit(&out);
	vinit(&argv);
	vinit(&lines);
	vinit(&fields);
	vinit(&seen);
	
	bwritestr(&out, "// auto generated by go tool dist\n"
		"\n");

	if(streq(goos, "linux")) {
		bwritestr(&out, "// +build !android\n\n");
	}
	
	bwritestr(&out,
		"package runtime\n"
		"import \"unsafe\"\n"
		"var _ unsafe.Pointer\n"
		"\n"
	);

	// Do not emit definitions for these.
	vadd(&seen, "true");
	vadd(&seen, "false");
	vadd(&seen, "raceenabled");
	vadd(&seen, "allgs");
	
	// Run 6c -D GOOS_goos -D GOARCH_goarch -I workdir -q -n -o workdir/runtimedefs
	// on each of the runtimedefs C files.
	vadd(&argv, bpathf(&b, "%s/%sc", tooldir, gochar));
	vadd(&argv, "-D");
	vadd(&argv, bprintf(&b, "GOOS_%s", goos));
	vadd(&argv, "-D");
	vadd(&argv, bprintf(&b, "GOARCH_%s", goarch));
	vadd(&argv, "-I");
	vadd(&argv, bprintf(&b, "%s", workdir));
	vadd(&argv, "-I");
	vadd(&argv, bprintf(&b, "%s/pkg/%s_%s", goroot, goos, goarch));
	vadd(&argv, "-q");
	vadd(&argv, "-n");
	vadd(&argv, "-o");
	vadd(&argv, bpathf(&b, "%s/runtimedefs", workdir));
	vadd(&argv, "");
	p = argv.p[argv.len-1];
	for(i=0; i<nelem(runtimedefs); i++) {
		argv.p[argv.len-1] = runtimedefs[i];
		runv(nil, dir, CheckExit, &argv);
		readfile(&b, bpathf(&b1, "%s/runtimedefs", workdir));
		bwriteb(&in, &b);
	}
	argv.p[argv.len-1] = p;
		
	// Process the aggregate output.
	skip = 0;
	splitlines(&lines, bstr(&in));
	for(i=0; i<lines.len; i++) {
		p = lines.p[i];
		// Drop comment and func lines.
		if(hasprefix(p, "//") || hasprefix(p, "func"))
			continue;
		
		// Note beginning of type or var decl, which can be multiline.
		// Remove duplicates.  The linear check of seen here makes the
		// whole processing quadratic in aggregate, but there are only
		// about 100 declarations, so this is okay (and simple).
		if(hasprefix(p, "type ") || hasprefix(p, "var ") || hasprefix(p, "const ")) {
			splitfields(&fields, p);
			if(fields.len < 2)
				continue;
			if(find(fields.p[1], seen.p, seen.len) >= 0) {
				if(streq(fields.p[fields.len-1], "{"))
					skip = 1;  // skip until }
				continue;
			}
			vadd(&seen, fields.p[1]);
		}

		// Const lines are printed in original case (usually upper). Add a leading _ as needed.
		if(hasprefix(p, "const ")) {
			if('A' <= p[6] && p[6] <= 'Z')
				bwritestr(&out, "const _");
			else
				bwritestr(&out, "const ");
			bwritestr(&out, p+6);
			continue;
		}

		if(skip) {
			if(hasprefix(p, "}"))
				skip = 0;
			continue;
		}
		
		bwritestr(&out, p);
	}

	// Some windows specific const.
	if(streq(goos, "windows")) {
		bwritestr(&out, bprintf(&b, "const cb_max = %d\n", MAXWINCB));
	}
	
	writefile(&out, file, 0);

	bfree(&in);
	bfree(&b);
	bfree(&b1);
	bfree(&out);
	vfree(&argv);
	vfree(&lines);
	vfree(&fields);
	vfree(&seen);
}
