#include <stdio.h>
#include <stdlib.h>
#include <mpi.h>
#include <complex.h>

#include <fftw3.h>
#include <pfft.h>
#include <pnfft.h>

void message(char *string, MPI_Comm comm);

int main(int argc, char **argv)
{
  int np[2];
  ptrdiff_t N[3];
  ptrdiff_t local_N[3], local_N_start[3], local_M;
  double lower_border[3], upper_border[3];
  MPI_Comm comm_cart_2d;
  pnfft_plan pnfft;
  pnfft_nodes nodes;
  pnfft_complex *f_hat;
  double *f, *x;
  
  /* initialize MPI and PFFT */
  MPI_Init(&argc, &argv);
  pnfft_init();
  
  /* set default values */
  np[0] = 2; np[1] = 2;
  N[0] = 2; N[1] = 2; N[2] = 4;
  local_M = 1;
  
  /* create two-dimensional process grid of size np[0] x np[1], if possible */
  if( pnfft_create_procmesh_2d(MPI_COMM_WORLD, np[0], np[1], &comm_cart_2d) ){
     pfft_fprintf(MPI_COMM_WORLD, stderr, "Error: This test file only works with 4 processors.\n");
    return 1;
  }
  
  /* get parameters of data distribution */
  pnfft_local_size_3d_c2r(N, comm_cart_2d, PNFFT_TRANSPOSED_NONE,
      local_N, local_N_start, lower_border, upper_border);
  
  /* plan parallel NFFT */
  pnfft = pnfft_init_3d_c2r(N, comm_cart_2d);

  /* initialize nodes */
  nodes = pnfft_init_nodes(local_M, PNFFT_MALLOC_X | PNFFT_MALLOC_F);

  /* get data pointers */
  f_hat = pnfft_get_f_hat(pnfft);
  f     = pnfft_get_f_real(nodes);
  x     = pnfft_get_x(nodes);

  /* initialize Fourier coefficients with random numbers */
  pnfft_init_f_hat_3d(N, local_N, local_N_start, PNFFT_TRANSPOSED_NONE,
      f_hat);
  
  /* print input data */
  pnfft_apr_complex_3d(
      f_hat, local_N, local_N_start, 0, "PNFFT, f_hat", MPI_COMM_WORLD);
  
  /* initialize nodes with random numbers */
  pnfft_init_x_3d(lower_border, upper_border, local_M,
      x);

  /* execute parallel NFFT */
  pnfft_trafo(pnfft, nodes, PNFFT_COMPUTE_F);
  
  /* print transformed data */
  pnfft_vpr_real(x, 3*local_M, "PNFFT, x", MPI_COMM_WORLD);
  pnfft_vpr_real(f, local_M, "PNFFT, f", MPI_COMM_WORLD);
  
  /* execute parallel adjoint NFFT */
  pnfft_adj(pnfft, nodes, PNFFT_COMPUTE_F);
  
  /* print result of adjoint NFFT */
  pnfft_apr_complex_3d(
      f_hat, local_N, local_N_start, 0, "PNFFT^H, f_hat", MPI_COMM_WORLD);

  /* free mem and finalize */
  pnfft_finalize(pnfft, PNFFT_FREE_F_HAT);
  pnfft_free_nodes(nodes, PNFFT_FREE_X | PNFFT_FREE_F);
  MPI_Comm_free(&comm_cart_2d);
  
  /* finalize MPI */
  MPI_Finalize();
  return 0;
}
