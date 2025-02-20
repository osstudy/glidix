#! /usr/bin/python
#	Copyright (c) 2014-2015, Madd Games.
#	All rights reserved.
#	
#	Redistribution and use in source and binary forms, with or without
#	modification, are permitted provided that the following conditions are met:
#	
#	* Redistributions of source code must retain the above copyright notice, this
#	  list of conditions and the following disclaimer.
#	
#	* Redistributions in binary form must reproduce the above copyright notice,
#	  this list of conditions and the following disclaimer in the documentation
#	  and/or other materials provided with the distribution.
#	
#	THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
#	AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
#	IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
#	DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
#	FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
#	DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
#	SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
#	CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
#	OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
#	OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import sys, os
import json

config = {}
f = open("config.json", "rb")
config = json.load(f)
f.close()

os.system("mkdir -p build")
os.system("mkdir -p out")

def doOperation(msg, op):
	sys.stderr.write(msg + " ")
	sys.stderr.flush()
	err = op()
	if err is None:
		sys.stderr.write("OK\n")
	else:
		sys.stderr.write("\n%s\n" % err)
		sys.exit(1)

f = open("build.rule", "rb")
rule = f.read()
f.close()

rules = []
objectFiles = []
depFiles = []
def makeRule(cfile):
	objfile = "build/%s.o" % (cfile.replace("/", "__")[:-2])
	depfile = objfile[:-2] + ".d"
	objectFiles.append(objfile)
	depFiles.append(depfile)

	out = rule
	out = out.replace("%DEPFILE%", depfile)
	out = out.replace("%OBJFILE%", objfile)
	out = out.replace("%CFILE%", cfile)
	out = out.replace("%REPLACE%", cfile.split("/")[-1][:-2]+".o")
	rules.append(out)

cfiles = []
def listdir(dirname):
	for name in os.listdir(dirname):
		path = dirname + "/" + name
		if os.path.isdir(path):
			listdir(path)
		elif path.endswith(".c"):
			cfiles.append(path)

listdir("src")
for cfile in cfiles:
	makeRule(cfile)

def opCreateBuildMK():
	f = open("build.mk", "wb")
	f.write(".PHONY: all\n")
	f.write("all: out/sh\n")
	f.write("TARGET_CC=%s\n" % config["compiler"])
	f.write("TARGET_AR=%s\n" % config["ar"])
	f.write("TARGET_RANLIB=%s\n" % config["ranlib"])
	f.write("PREFIX=%s\n" % config["prefix"])
	f.write("CFLAGS=-mno-mmx -mno-sse -mno-sse2 -I include -Wall -Werror\n")
	f.write("out/sh: %s\n" % (" ".join(objectFiles)))
	f.write("\t%s -o $@ $^\n" % config["compiler"])
	f.write("-include %s\n" % (" ".join(depFiles)))
	f.write("\n".join(rules))
	f.close()

doOperation("creating build.mk...", opCreateBuildMK)
sys.stderr.write("running the build...\n")
if os.system("make -f build.mk") != 0:
	sys.exit(1)
