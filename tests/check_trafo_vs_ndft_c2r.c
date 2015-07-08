#include <stdlib.h>
#include <complex.h>
#include <pnfft.h>


static void pnfft_perform_guru(
    const ptrdiff_t *N, const ptrdiff_t *n, ptrdiff_t local_M,
    int m, const double *x_max, unsigned window_flag,
    const int *np, MPI_Comm comm);

static void init_input(const ptrdiff_t *N, const ptrdiff_t *local_N, const ptrdiff_t *local_N_start, double complex *data);
static void init_parameters(
    int argc, char **argv,
    ptrdiff_t *N, ptrdiff_t *n, ptrdiff_t *local_M,
    int *m, int *window,
    double *x_max, int *np);
static double compare_complex_real(const ptrdiff_t local_M,
    const double complex *data_c2c, const double *data_c2r,
    MPI_Comm comm);


int main(int argc, char **argv){
  int np[3], m, window;
  unsigned window_flag;
  ptrdiff_t N[3], n[3], local_M;
  double x_max[3];
  
  MPI_Init(&argc, &argv);
  pnfft_init();
  
  /* set default values */
  N[0] = N[1] = N[2] = 16;
  n[0] = n[1] = n[2] = 0;
  local_M = 0;
  m = 6;
  window = 4;
  x_max[0] = x_max[1] = x_max[2] = 0.5;
  np[0]=2; np[1]=2;
  
  /* set parameters by command line */
  init_parameters(argc, argv, N, n, &local_M, &m, &window, x_max, np);

  /* if M or n are set to zero, we choose nice values */
  local_M = (local_M==0) ? N[0]*N[1]*N[2]/(np[0]*np[1]) : local_M;
  for(int t=0; t<3; t++)
    n[t] = (n[t]==0) ? 2*N[t] : n[t];

  switch(window){
    case 0: window_flag = PNFFT_WINDOW_GAUSSIAN; break;
    case 1: window_flag = PNFFT_WINDOW_BSPLINE; break;
    case 2: window_flag = PNFFT_WINDOW_SINC_POWER; break;
    case 3: window_flag = PNFFT_WINDOW_BESSEL_I0; break;
    default: window_flag = PNFFT_WINDOW_KAISER_BESSEL;
  }

  pfft_printf(MPI_COMM_WORLD, "******************************************************************************************************\n");
  pfft_printf(MPI_COMM_WORLD, "* Computation of parallel NFFT\n");
  pfft_printf(MPI_COMM_WORLD, "* for  N[0] x N[1] x N[2] = %td x %td x %td Fourier coefficients (change with -pnfft_N * * *)\n", N[0], N[1], N[2]);
  pfft_printf(MPI_COMM_WORLD, "* at   local_M = %td nodes per process (change with -pnfft_local_M *)\n", local_M);
  pfft_printf(MPI_COMM_WORLD, "* with n[0] x n[1] x n[2] = %td x %td x %td FFT grid size (change with -pnfft_n * * *),\n", n[0], n[1], n[2]);
  pfft_printf(MPI_COMM_WORLD, "*      m = %d real space cutoff (change with -pnfft_m *),\n", m);
  pfft_printf(MPI_COMM_WORLD, "*      window = %d window function ", window);
  switch(window){
    case 0: pfft_printf(MPI_COMM_WORLD, "(PNFFT_WINDOW_GAUSSIAN) "); break;
    case 1: pfft_printf(MPI_COMM_WORLD, "(PNFFT_WINDOW_BSPLINE) "); break;
    case 2: pfft_printf(MPI_COMM_WORLD, "(PNFFT_WINDOW_SINC_POWER) "); break;
    case 3: pfft_printf(MPI_COMM_WORLD, "(PNFFT_WINDOW_BESSEL_I0) "); break;
    default: pfft_printf(MPI_COMM_WORLD, "(PNFFT_WINDOW_KAISER_BESSEL) "); break;
  }
  pfft_printf(MPI_COMM_WORLD, "(change with -pnfft_window *),\n");
  pfft_printf(MPI_COMM_WORLD, "* on   np[0] x np[1] = %td x %td processes (change with -pnfft_np * *)\n", np[0], np[1]);
  pfft_printf(MPI_COMM_WORLD, "*******************************************************************************************************\n\n");


  /* calculate parallel NFFT */
  pnfft_perform_guru(N, n, local_M, m,   x_max, window_flag, np, MPI_COMM_WORLD);

  /* free mem and finalize */
  pnfft_cleanup();
  MPI_Finalize();
  return 0;
}


static void pnfft_perform_guru(
    const ptrdiff_t *N, const ptrdiff_t *n, ptrdiff_t local_M,
    int m, const double *x_max, unsigned window_flag,
    const int *np, MPI_Comm comm
    )
{
  int myrank;
  double time, time_max;
  MPI_Comm comm_cart_2d;

  ptrdiff_t local_N_c2c[3], local_N_start_c2c[3];
  double lower_border_c2c[3], upper_border_c2c[3];
  pnfft_plan plan_c2c;
  pnfft_complex *f_hat_c2c, *f_c2c;
  double *x_c2c;

  ptrdiff_t local_N_c2r[3], local_N_start_c2r[3];
  double lower_border_c2r[3], upper_border_c2r[3];
  pnfft_plan plan_c2r;
  pnfft_complex *f_hat_c2r;
  double *x_c2r, *f_c2r;

  /* create three-dimensional process grid of size np[0] x np[1], if possible */
  if( pnfft_create_procmesh(2, comm, np, &comm_cart_2d) ){
    pfft_fprintf(comm, stderr, "Error: Procmesh of size %d x %d does not fit to number of allocated processes.\n", np[0], np[1]);
    pfft_fprintf(comm, stderr, "       Please allocate %d processes (mpiexec -np %d ...) or change the procmesh (with -pnfft_np * *).\n", np[0]*np[1], np[0]*np[1]);
    MPI_Finalize();
    exit(1);
  }

  MPI_Comm_rank(comm_cart_2d, &myrank);

  /* get parameters of data distribution */
  pnfft_local_size_guru(3, N, n, x_max, m, comm_cart_2d, PNFFT_TRANSPOSED_NONE,
      local_N_c2c, local_N_start_c2c, lower_border_c2c, upper_border_c2c);
  pnfft_local_size_guru_c2r(3, N, n, x_max, m, comm_cart_2d, PNFFT_TRANSPOSED_NONE,
      local_N_c2r, local_N_start_c2r, lower_border_c2r, upper_border_c2r);

  /* plan parallel NFFT */
  plan_c2c = pnfft_init_guru(3, N, n, x_max, local_M, m,
      PNFFT_MALLOC_X| PNFFT_MALLOC_F_HAT| PNFFT_MALLOC_F| window_flag, PFFT_ESTIMATE,
      comm_cart_2d);
  plan_c2r = pnfft_init_guru_c2r(3, N, n, x_max, local_M, m,
      PNFFT_MALLOC_X| PNFFT_MALLOC_F_HAT| PNFFT_MALLOC_F| window_flag, PFFT_ESTIMATE,
      comm_cart_2d);

  /* get data pointers */
  f_hat_c2c = pnfft_get_f_hat(plan_c2c);
  f_c2c     = pnfft_get_f(plan_c2c);
  x_c2c     = pnfft_get_x(plan_c2c);
  f_hat_c2r = pnfft_get_f_hat(plan_c2r);
  f_c2r     = pnfft_get_f_real(plan_c2r);
  x_c2r     = pnfft_get_x(plan_c2r);

  /* Initialize Fourier coefficients with random numbers */
  init_input(N, local_N_c2c, local_N_start_c2c, f_hat_c2c);
  init_input(N, local_N_c2r, local_N_start_c2r, f_hat_c2r);

  /* Initialize nodes with random numbers */
  pnfft_init_x_3d(lower_border_c2c, upper_border_c2c, local_M, x_c2c);
  for (int k=0; k<local_M*3; k++)
    x_c2r[k] = x_c2c[k];

  /* execute parallel NFFT */
  time = -MPI_Wtime();
  pnfft_direct_trafo(plan_c2c);
  time += MPI_Wtime();
  
  /* print timing */
  MPI_Reduce(&time, &time_max, 1, MPI_DOUBLE, MPI_MAX, 0, comm);
  pfft_printf(comm, "c2c pnfft_direct_trafo needs %6.2e s\n", time_max);
 
  /* execute parallel NDFT */
  time = -MPI_Wtime();
  pnfft_direct_trafo(plan_c2r);
  time += MPI_Wtime();

  /* print timing */
  MPI_Reduce(&time, &time_max, 1, MPI_DOUBLE, MPI_MAX, 0, comm);
  pfft_printf(comm, "c2r pnfft_direct_trafo needs %6.2e s\n", time_max);

  /* calculate error of PNFFT */
  double err = compare_complex_real(local_M, f_c2c, f_c2r, comm_cart_2d);
  pfft_printf(MPI_COMM_WORLD, "max error between c2c pndft and c2r pndft: %6.2e\n", err);

  /* free mem and finalize */
  pnfft_finalize(plan_c2c, PNFFT_FREE_X | PNFFT_FREE_F | PNFFT_FREE_F_HAT);
  pnfft_finalize(plan_c2r, PNFFT_FREE_X | PNFFT_FREE_F | PNFFT_FREE_F_HAT);
  MPI_Comm_free(&comm_cart_2d);
}


static void init_parameters(
    int argc, char **argv,
    ptrdiff_t *N, ptrdiff_t *n, ptrdiff_t *local_M,
    int *m, int *window,
    double *x_max, int *np
    )
{
  pfft_get_args(argc, argv, "-pnfft_local_M", 1, PFFT_PTRDIFF_T, local_M);
  pfft_get_args(argc, argv, "-pnfft_N", 3, PFFT_PTRDIFF_T, N);
  pfft_get_args(argc, argv, "-pnfft_n", 3, PFFT_PTRDIFF_T, n);
  pfft_get_args(argc, argv, "-pnfft_np", 2, PFFT_INT, np);
  pfft_get_args(argc, argv, "-pnfft_m", 1, PFFT_INT, m);
  pfft_get_args(argc, argv, "-pnfft_window", 1, PFFT_INT, window);
  pfft_get_args(argc, argv, "-pnfft_x_max", 3, PFFT_DOUBLE, x_max);
}

#define DATA_INIT(i) (( (double)1000 ) / ( (double)( (i) == 0 ? 1 : i) ))

static pfft_complex semirandom(const ptrdiff_t *N, ptrdiff_t k0, ptrdiff_t k1, ptrdiff_t k2) {
  if ((k0 == -N[0]/2) || (k1 == -N[1]/2) || (k2 == -N[2]/2))
    return 0 + 0*I;
  if (k0 == (N[0]+1)/2)
    return semirandom(N, -k0, k1, k2);
  if (k1 == (N[1]+1)/2)
    return semirandom(N, k0, -k1, k2);
  if (k2 == (N[2]+1)/2)
    return semirandom(N, k0, k1, -k2);

  ptrdiff_t l0 = (k0) ? k0+N[0] : 1;
  ptrdiff_t l1 = (k1) ? k1-2*N[1] : 1;
  ptrdiff_t l2 = (k2) ? k2+N[2] : 1;

  double re = DATA_INIT(l0+l1*l2) + 3*DATA_INIT(l0*l1+l2) + DATA_INIT(l2*l0+l1);
  double im = 3*DATA_INIT(l0+l1*l1) + DATA_INIT(l1+l2*l2) - DATA_INIT(l2+l0*l0);

  return re + im*I;
}

static void init_input(const ptrdiff_t *N, const ptrdiff_t *local_N, const ptrdiff_t *local_N_start, pfft_complex *data)
{
  int m = 0;
  for(ptrdiff_t k0=local_N_start[0]; k0<local_N_start[0]+local_N[0]; k0++)
    for(ptrdiff_t k1=local_N_start[1]; k1<local_N_start[1]+local_N[1]; k1++)
      for(ptrdiff_t k2=local_N_start[2]; k2<local_N_start[2]+local_N[2]; k2++, m++)
        data[m] = semirandom(N, k0, k1, k2) + conj(semirandom(N, -k0, -k1, -k2));
}

static double compare_complex_real(const ptrdiff_t local_M,
    const double complex *data_c2c, const double *data_c2r,
    MPI_Comm comm)
{
  double err = 0, max_err = 0;
  double glob_max_err, re, im;

  for (int k=0; k<local_M; k++) {
    re = creal(data_c2c[k]) - data_c2r[k];
    im = cimag(data_c2c[k]);
    err = sqrt(re*re + im*im);
    if (err > max_err)
      max_err = err;
    if (err > 1)
      printf("k: %d, c2c: %.3e %.3ei, c2r: %.3e, err: %.3e\n", k, creal(data_c2c[k]), cimag(data_c2c[k]), data_c2r[k], err);
  }

  MPI_Allreduce(&max_err, &glob_max_err, 1, MPI_DOUBLE, MPI_MAX, comm);
  return glob_max_err;
}

