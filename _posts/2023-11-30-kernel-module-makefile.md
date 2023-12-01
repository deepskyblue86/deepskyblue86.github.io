---
layout: post
title: A linux kernel module adventure
footer-extra: creativecommons.html
tags: [linux, kernel, makefile]
---

In this article I want to share with you what I learned about kernel modules and makefiles.

## The problem
Usually, to deal with breaking changes in the kernel, one would check the kernel version,
but when the breaking changes get backported, that check won't help.
<br/>
In the past<sup>[1](#ref-1)</sup>, Falco encountered this issue only with RHEL-derived distros, which expose the `RHEL_RELEASE_CODE` macro, so we could check that one.<br/>
One day, I had to ensure compatibility with EulerOS, which is a RHEL-like distro, but doesn't expose the `RHEL_RELEASE_CODE` macro, nor any other macro which could be used for the same purpose.<br/>
Short after, we had a similar issue with RHEL too, which backported a breaking change within the same major/minor version, making it impossible to check with their macro too.

To be more specific, the issue was the change [96d4f267e](https://github.com/torvalds/linux/commit/96d4f267e), introduced in 5.0 and backported by RHEL 8.1.
The code to deal with that was the following:
```c
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(5, 0, 0)) || (PPM_RHEL_RELEASE_CODE > 0 && PPM_RHEL_RELEASE_CODE >= PPM_RHEL_RELEASE_VERSION(8, 1))
#define ppm_access_ok(type, addr, size)	access_ok(addr, size)
#else
#define ppm_access_ok(type, addr, size)	access_ok(type, addr, size)
#endif
```

## The idea

Popular build systems like `cmake` and `autotools` have a way to ensure compatibility with preliminary checks to verify if a piece of code compiles. This is done in a phase usually called *configure*.
<br/>
So I thought about having minimal sub-modules to be compiled just to check if the kernel has a specific feature, and export a macro out of that build.

The code for the `access_ok` would then be:
```c
#ifdef HAS_ACCESS_OK_2
#define ppm_access_ok(type, addr, size)	access_ok(addr, size)
#else
#define ppm_access_ok(type, addr, size)	access_ok(type, addr, size)
#endif
```
Easier said than done, of course.

### The Falco kernel module makefile

If we look at the Falco kernel module makefile, it doesn't look complex, nor alike any "regular" makefile:
```makefile
#
# Copyright (C) 2022 The Falco Authors.
#
# This file is dual licensed under either the MIT or GPL 2. See
# MIT.txt or GPL.txt for full copies of the license.
#

falco-y += main.o dynamic_params_table.o fillers_table.o flags_table.o ppm_events.o ppm_fillers.o event_table.o syscall_table32.o syscall_table64.o ppm_cputime.o ppm_tp.o socketcall_to_syscall.o
obj-m += falco.o
ccflags-y := 

KERNELDIR       ?= /lib/modules/$(shell uname -r)/build

TOP := $(shell pwd)
all:
    $(MAKE) -C $(KERNELDIR) M=$(TOP) modules

clean:
    $(MAKE) -C $(KERNELDIR) M=$(TOP) clean

install: all
    $(MAKE) -C $(KERNELDIR) M=$(TOP) modules_install
```

I discovered (the hard way) that those targets were just convenience targets, if one wants to build the module from its directory just running `make`.

### The anatomy of a kernel module makefile
The simplest `Makefile` for a kernel module is:

```makefile
testmod-y += test.o
obj-m += testmod.o
```
This is sufficient to build a kernel module named `testmod.ko` from a single source file `test.c`.
<br/>
It immediately catches the eye that there are no targets, but only variables.
How does it work then? Everything is taken care of by the so-called "*kbuild*" build system<sup>[2](#ref-2)</sup>, which is a mix of makefiles, scripts and other tools.
<br/>
Assuming our module is in the current directory, that's how we build it:
```sh
make -C /lib/modules/`uname -r`/build M=${PWD} modules
```
The `-C` switch tells `make` to change directory before starting the build,
while the `M` variable is used by the kernel's makefile to know where to find
the module.

### Recursive and multistep too!

As we just learned, *kbuild* includes our module makefile, but is it just that?
Of course not, it's multistep too!
<br/>
Now, *make* keeps a list of the included makefiles, and we can access it with the `MAKEFILE_LIST` variable.
If we change our makefile to print it, not only we can see that it's passed on multiple times, but also what drives that:
```makefile
testmod-y += test.o
obj-m += testmod.o

$(info MAKEFILE_LIST: $(MAKEFILE_LIST))
```
We'll have:
```
MAKEFILE_LIST: scripts/Makefile.build include/config/auto.conf scripts/Kbuild.include scripts/Makefile.compiler /tmp/foo/ACCESS_OK_2/Makefile
  CC [M]  /tmp/foo/ACCESS_OK_2/test.o
  LD [M]  /tmp/foo/ACCESS_OK_2/testmod.o
MAKEFILE_LIST: scripts/Makefile.modpost include/config/auto.conf scripts/Kbuild.include /tmp/foo/ACCESS_OK_2/Makefile
  MODPOST /tmp/foo/ACCESS_OK_2/Module.symvers
  CC [M]  /tmp/foo/ACCESS_OK_2/testmod.mod.o
  LD [M]  /tmp/foo/ACCESS_OK_2/testmod.ko
  BTF [M] /tmp/foo/ACCESS_OK_2/testmod.ko
```

By testing that on different distros, I found out that the first one is always `scripts/Makefile.build`, so I used that to avoid performing my "*configure*" step multiple times.

### All the caveats

Of course the devil's in the details...
- In most distros I had just `scripts/Makefile.build`, but some had a prefix path, so I had to learn how to manipulate the path to get the file and its parent directory<sup>[3](#ref-3)</sup>. That's the result:
```makefile
# ...
FIRST_MAKEFILE := $(firstword $(MAKEFILE_LIST))
FIRST_MAKEFILE_FILENAME := $(notdir $(FIRST_MAKEFILE))
FIRST_MAKEFILE_DIRNAME := $(shell basename $(dir $(FIRST_MAKEFILE)))
ifeq ($(FIRST_MAKEFILE_DIRNAME)/$(FIRST_MAKEFILE_FILENAME), scripts/Makefile.build)
# ...
```
- I put a `Makefile.in` file to be included, run `make` for the sub-module and eventually append the flag to `ccflags-y`. That didn't work because when we get there *kbuild* already set everything up to build the main module, thus we have to start clean. I achieved that by using a wrapper script, and calling it with `env -i`. <br/>Done? Of course not. Some distros would use accessory tools, and I needed to pass `PATH` to get them working.

## Everything together

Here you can see it in action:
```sh
❯ make -C /lib/modules/`uname -r`/build M=${PWD} modules
make: Entering directory '/usr/src/kernels/5.4.17-2102.204.4.4.el8uek.x86_64'
[configure] Including /usr/src/scap-6.1.0-191+300ba15-driver//configure/DEVNODE_ARG1_CONST/Makefile.inc /usr/src/scap-6.1.0-191+300ba15-driver//configure/ACCESS_OK_2/Makefile.inc
[configure] Build output for HAS_DEVNODE_ARG1_CONST:
[configure] make: Entering directory '/usr/src/scap-6.1.0-191+300ba15-driver/configure/DEVNODE_ARG1_CONST' make -C /lib/modules/5.4.17-2102.204.4.4.el8uek.x86_64/build M=/usr/src/scap-6.1.0-191+300ba15-driver/configure/DEVNODE_ARG1_CONST modules make[1]: Entering directory '/usr/src/kernels/5.4.17-2102.204.4.4.el8uek.x86_64'   CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/configure/DEVNODE_ARG1_CONST/test.o /usr/src/scap-6.1.0-191+300ba15-driver/configure/DEVNODE_ARG1_CONST/test.c: In function 'devnode_dev_const_init': /usr/src/scap-6.1.0-191+300ba15-driver/configure/DEVNODE_ARG1_CONST/test.c:29:14: error: initialization of 'char * (*)(struct device *, umode_t *)' {aka 'char * (*)(struct device *, short unsigned int *)'} from incompatible pointer type 'char * (*)(const struct device *, umode_t *)' {aka 'char * (*)(const struct device *, short unsigned int *)'} [-Werror=incompatible-pointer-types]    .devnode = ppm_devnode               ^~~~~~~~~~~ /usr/src/scap-6.1.0-191+300ba15-driver/configure/DEVNODE_ARG1_CONST/test.c:29:14: note: (near initialization for 'g_ppm_class.devnode') cc1: some warnings being treated as errors make[2]: *** [scripts/Makefile.build:262: /usr/src/scap-6.1.0-191+300ba15-driver/configure/DEVNODE_ARG1_CONST/test.o] Error 1 make[1]: *** [Makefile:1786: /usr/src/scap-6.1.0-191+300ba15-driver/configure/DEVNODE_ARG1_CONST] Error 2 make[1]: Leaving directory '/usr/src/kernels/5.4.17-2102.204.4.4.el8uek.x86_64' make: *** [Makefile:15: all] Error 2 make: Leaving directory '/usr/src/scap-6.1.0-191+300ba15-driver/configure/DEVNODE_ARG1_CONST'
[configure] Setting HAS_ACCESS_OK_2 flag
  CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/main.o
  CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/dynamic_params_table.o
  CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/fillers_table.o
  CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/flags_table.o
  CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/ppm_events.o
  CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/ppm_fillers.o
  CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/event_table.o
  CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/syscall_table32.o
  CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/syscall_table64.o
  CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/ppm_cputime.o
  CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/ppm_tp.o
  CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/socketcall_to_syscall.o
  LD [M]  /usr/src/scap-6.1.0-191+300ba15-driver/scap.o
  Building modules, stage 2.
  MODPOST 1 modules
  CC [M]  /usr/src/scap-6.1.0-191+300ba15-driver/scap.mod.o
  LD [M]  /usr/src/scap-6.1.0-191+300ba15-driver/scap.ko
make: Leaving directory '/usr/src/kernels/5.4.17-2102.204.4.4.el8uek.x86_64'
```

The output of the *configure* mechanism is clearly marked, and it gets really verbose when the sub-module build fails, in order to help debugging if necessary.

Seeing `Setting HAS_ACCESS_OK_2 flag` the first time was really satisfying 🤩.

You can check out the code in the [Falco Libs repository](https://github.com/falcosecurity/libs/pull/1452).

<br/>

---


# References
<div id="ref-1">
1. <a href="https://github.com/falcosecurity/libs/commit/45e7a735f">Falco: RHEL 8.1 backports the definition of access_ok from 5.0</a>
</div>
<div id="ref-2">
2. <a href="https://www.kernel.org/doc/html/latest/kbuild/modules.html">Linux kernel documentation: kbuild/modules</a>
</div>
<div id="ref-3">
3. <a href="https://www.gnu.org/software/make/manual/html_node/File-Name-Functions.html">GNU make manual: Functions for File Names</a>
</div>
