
dnl ---------------------------------------------
dnl translate easy to remember target names into recognizable gnu variants and
dnl test the cross compilation platform and adjust default settings

AC_DEFUN(CODA_SETUP_BUILD,

[AC_SUBST(LIBTOOL_LDFLAGS)

case ${target} in
  djgpp | win95 | dos )  target=i386-pc-msdos ;;
  cygwin* | winnt | nt ) target=i386-pc-cygwin ;;
  arm ) target=arm-unknown-linux-gnuelf ;;
esac

AC_CANONICAL_SYSTEM
host=${target}
program_prefix=

dnl shared libs don't work on most platforms due to non-PIC code in process.s
case ${host} in
   *sun-solaris* )
    echo "Setting special conditions for Solaris"
    AFLAGS="-traditional"

    if [ ${host_cpu} != i386 ] ; then
	AM_DISABLE_SHARED
    fi
    ;;
   i*86-* )
    dnl Shared libs seem to work for i386-based platforms
    ;;
   * )
    AC_DISABLE_SHARED
    ;;
esac

if test ${build} != ${host} ; then
  case ${host} in
   i*86-pc-msdos )
    dnl no shared libs for dos
    AM_DISABLE_SHARED

    CC="dos-gcc -bmmap"
    CXX="dos-gcc -bmmap"
    AR="dos-ar"
    RANLIB="true"
    AS="dos-as"
    NM="dos-nm"

    dnl We have to override some things the configure script tends to
    dnl get wrong as it tests the build platform feature
    ac_cv_func_mmap_fixed_mapped=yes
    ;;
   i*86-pc-cygwin )
    dnl -D__CYGWIN32__ should be defined but sometimes isn't (wasn't?)
    CC="gnuwin32gcc -D__CYGWIN32__"
    CXX="gnuwin32g++"
    AR="gnuwin32ar"
    RANLIB="gnuwin32ranlib"
    AS="gnuwin32as"
    NM="gnuwin32nm"
    DLLTOOL="gnuwin32dlltool"
    OBJDUMP="gnuwin32objdump"

    LDFLAGS="-L/usr/gnuwin32/lib"

    dnl We need these to get a dll built
    libtool_flags="--enable-win32-dll"
    LIBTOOL_LDFLAGS="-no-undefined"
    ;;
   arm-unknown-linux-gnuelf )
    CC="arm-unknown-linuxelf-gcc"
    AR="arm-unknown-linuxelf-ar"
    RANLIB="arm-unknown-linuxelf-ranlib"
    AS="arm-unknown-linuxelf-as"
    NM="arm-unknown-linuxelf-nm"
    OBJDUMP="arm-unknown-linuxelf-objdump"
    ;;
 esac
fi])

