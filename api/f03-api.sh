#! /bin/sh

# Script to generate Fortran 2003 interface declarations for FFTW from
# the fftw3.h header file.

# Use this script in the following way:
# ./f03-api.sh d f > pnfft.f03.in
# ./f03-api.sh l > pnfftl.f03.in

# This is designed so that the Fortran caller can do:
#   use, intrinsic :: iso_c_binding
#   implicit none
#   include 'fftw3.f03'
# and then call the C FFTW functions directly, with type checking.


# pnfft.h depends on pfft.h, fftw3-mpi.h, and fftw3.h.
# set these paths such that the preprocessor can find the required headers
PFFT_INC=$HOME/local/pfft-1.0.8-alpha/include
FFTW_INC=$HOME/local/fftw-3.3.4/include

if [ ! -e $FFTW_INC/fftw3-mpi.h ]; then
  echo "Error: Correct include path to fftw3-mpi.h needed."
  exit 1
fi
if [ ! -e $PFFT_INC/pfft.h ]; then
  echo "Error: Correct include path to pfft.h needed."
  exit 1
fi

echo "! Generated automatically.  DO NOT EDIT!"

# C_FFTW_R2R_KIND is determined by configure and inserted by the Makefile
# echo "  integer, parameter :: C_FFTW_R2R_KIND = @C_FFTW_R2R_KIND@"

# Extract constants
echo
echo "! integers"
perl -pe 's/([A-Z0-9_]+)=([+-]?[0-9]+)/\n  integer\(C_INT\), parameter :: \1 = \2\n/g' < pnfft.h | grep 'integer(C_INT)'
echo
echo "! unsigned"
perl -pe 's/#define +([A-Z0-9_]+) +\(([+-]?[0-9]+)U?\)/\n  integer\(C_INT\), parameter :: \1 = \2\n/g' < pnfft.h | grep 'integer(C_INT)'
echo
echo "! shifted unsigned"
perl -pe 'if (/#define +([A-Z0-9_]+) +\(([0-9]+)U? *<< *([0-9]+)\)/) { print "\n  integer\(C_INT\), parameter :: $1 = ",$2 << $3,"\n"; }' < pnfft.h | grep 'integer(C_INT)'
echo
echo "! mixed shifted unsigned and redirections"
perl -pe 'if (/#define +([A-Z0-9_]+) +\(\( *\(([0-9]+)U? *<< *([0-9]+)\) +([A-Z0-9_| ]+)\)\)/) { print "\n  integer\(C_INT\), parameter :: $1 = ",$2 << $3," $4\n"; }' < pnfft.h | grep 'integer(C_INT)' | sed 's/| / \&\n      + /g'
echo
echo "! redirections"
perl -pe 'if (/#define +([A-Z0-9_]+) +\(\(([A-Z0-9_| ]+)\)\)/) { print "\n  integer\(C_INT\), parameter :: $1 = $2\n"; }' < pnfft.h | grep 'integer(C_INT)' | sed 's/| / \&\n      + /g'

# Extract function declarations
for p in $*; do
    if test "$p" = "d"; then p=""; fi

    echo
#     cat <<EOF
#   type, bind(C) :: fftw${p}_mpi_ddim
#      integer(C_INTPTR_T) n, ib, ob
#   end type fftw${p}_mpi_ddim   cat <<EOF
# EOF

    echo
    echo "  interface"
    mpicc -E pnfft.h -I${FFTW_INC} -I${PFFT_INC} |grep "pnfft${p}_init" |tr ';' '\n' |grep -v "pnfft${p}_get_args" | perl genf03-api.pl
    echo "  end interface"

done
