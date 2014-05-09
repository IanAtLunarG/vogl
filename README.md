vogl_chroot
=============

## Enlist ##

```
mkdir vogl_extbuild  ; place chroot temp files here (otherwise goes in vogl_chroot/vogl_extbuild)
git clone https://bitbucket.org/raddebugger/vogl_chroot.git  
cd vogl_chroot  
git clone https://github.com/ValveSoftware/vogl.git  
```

## Build ##

To build the vogl chroots (uses schroot), do the following:

    ./bin/chroot_build.sh --i386 --amd64

You should now be ready to build in your chroots. Something like any of these:

    ./bin/mkvogl.sh --release --amd64
    ./bin/mkvogl.sh --debug --amd64 --i386 --clang34 --verbose
    ./bin/mkvogl.sh --release --amd64 --i386 --gcc48 --VOGL_ENABLE_ASSERTS

Note that you do _not_ have to use the chroots or mkvogl.sh to build. You could do your own cmake (cmake vogl_chroot) and go from there. It's up to you to get the dependencies correct though. Look at vogl/bin/chroot_configure.sh to see how the chroots are set up. The source for mkvogl is in ./bin/src/mkvogl.cpp - it's just a simple cpp wrapper around cmake.

If you do use the chroots, do not build from within an encrypted home folder, as files in an encrypted home folder will not be visible from within the chroot, causing the build script to fail.

## Capturing ##

    ./bin/steamlauncher.sh --gameid ./vogl_build/bin/glxspheres32
    ./bin/steamlauncher.sh --gameid ./vogl_build/bin/glxspheres64 --amd64

You should now have something like the following in your temp directory:

    /tmp/vogltrace.glxspheres64.2014_01_20-16_19_34.bin

## Replay ##

    ./vogl_build/bin/voglreplay64 /tmp/vogltrace.glxspheres64.2014_01_20-16_19_34.bin

or

    ./vogl_build/bin/vogleditor64 /tmp/vogltrace.glxspheres64.2014_01_20-16_19_34.bin

## Directory structure ##

The directory structure for vogl currently looks like this:

        vogl_chroot/
            bin/
                chroot_build.sh ; script to build/rebuild chroots
                chroot_configure.sh ; script to build libs to chroots (used by chroot_build.sh)
                set_compiler.sh ; switch chroot default compiler
            external/ ; external source (libunwind, etc.)
            vogl/ ; vogl source
            vogl_build/
                bin/ ; destination for binaries
            vogl_extbuild/
                i386/   ; external projects untar'd & built here
                x86_64/ ;
