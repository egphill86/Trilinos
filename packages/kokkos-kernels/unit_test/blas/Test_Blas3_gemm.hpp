#include<gtest/gtest.h>
#include<Kokkos_Core.hpp>
#include<Kokkos_Random.hpp>
#include<KokkosBlas3_gemm.hpp>
#include<KokkosKernels_TestUtils.hpp>

namespace Test {

  template<class ViewTypeA, class ViewTypeB, class ViewTypeC, class ExecutionSpace>
  struct gemm_VanillaGEMM {
    bool A_t, B_t, A_c, B_c;
    int N,K;
    ViewTypeA A;
    ViewTypeB B;
    ViewTypeC C;

    typedef typename ViewTypeA::value_type ScalarA;
    typedef typename ViewTypeB::value_type ScalarB;
    typedef typename ViewTypeC::value_type ScalarC;
    typedef Kokkos::Details::ArithTraits<ScalarC> APT;
    typedef typename APT::mag_type mag_type;
    ScalarA alpha;
    ScalarC beta;

    KOKKOS_INLINE_FUNCTION
    void operator() (const typename Kokkos::TeamPolicy<ExecutionSpace>::member_type& team) const {
// GNU COMPILER BUG WORKAROUND
#if defined(KOKKOS_COMPILER_GNU) && !defined(__CUDA_ARCH__) && !defined(__HIP_DEVICE_COMPILE__)
      int i = team.league_rank();
#else
      const int i = team.league_rank();
#endif
      Kokkos::parallel_for(Kokkos::TeamThreadRange(team,N), [&] (const int& j) {
        ScalarC C_ij = 0.0;

        // GNU 5.3, 5.4 and 6.1 (and maybe more) crash with another nested lambda here

#if defined(KOKKOS_COMPILER_GNU) && !defined(KOKKOS_COMPILER_NVCC)
        for(int k=0; k<K; k++) {
          ScalarA A_ik = A_t?(A_c?APT::conj(A(k,i)):A(k,i)):A(i,k);
          ScalarB B_kj = B_t?(B_c?APT::conj(B(j,k)):B(j,k)):B(k,j);
          C_ij += A_ik*B_kj;
        }
#else
        Kokkos::parallel_reduce(Kokkos::ThreadVectorRange(team,K), [&] (const int& k, ScalarC& lsum) {
           ScalarA A_ik = A_t?(A_c?APT::conj(A(k,i)):A(k,i)):A(i,k);
           ScalarB B_kj = B_t?(B_c?APT::conj(B(j,k)):B(j,k)):B(k,j);
           lsum += A_ik*B_kj;
        },C_ij);
#endif

        C(i,j) = beta*C(i,j) + alpha*C_ij;
      });
    }
  };

  template<class ViewTypeC, class ExecutionSpace>
  struct DiffGEMM {
    int N;
    ViewTypeC C,C2;

    typedef typename ViewTypeC::value_type ScalarC;
    typedef Kokkos::Details::ArithTraits<ScalarC> APT;
    typedef typename APT::mag_type mag_type;

    KOKKOS_INLINE_FUNCTION
    void operator() (const typename Kokkos::TeamPolicy<ExecutionSpace>::member_type& team, mag_type& diff) const {
      const int i = team.league_rank();
      mag_type diff_row = 0;
      Kokkos::parallel_reduce(Kokkos::TeamThreadRange(team,N), [&] (const int& j,mag_type& diff_ij) {
        //printf("A (%i %i) (%i %i) (%i %i)\n",C.extent(0),C.extent(1),C2.extent(0),C2.extent(1),i,j);
        diff_ij += APT::abs(C(i,j)-C2(i,j));
        //printf("B (%i %i) (%i %i) (%i %i)\n",C.extent(0),C.extent(1),C2.extent(0),C2.extent(1),i,j);
      },diff_row);
      Kokkos::single(Kokkos::PerTeam(team), [&] () {
        diff += diff_row;
      });
    }
  };

  template<class ViewTypeA, class ViewTypeB, class ViewTypeC, class Device>
  void impl_test_gemm(const char* TA, const char* TB, int M, int N, int K,
      typename ViewTypeA::value_type alpha,
      typename ViewTypeC::value_type beta) {


    bool A_t = (TA[0]!='N') && (TA[0]!='n');
    bool B_t = (TB[0]!='N') && (TB[0]!='n');
    bool A_c = (TA[0]=='C') || (TA[0]=='c');
    bool B_c = (TB[0]=='C') || (TB[0]=='c');
    typedef typename ViewTypeA::device_type::execution_space execution_space;
    typedef typename ViewTypeA::value_type ScalarA;
    typedef typename ViewTypeB::value_type ScalarB;
    typedef typename ViewTypeC::value_type ScalarC;
    typedef Kokkos::Details::ArithTraits<ScalarC> APT;
    typedef typename APT::mag_type mag_type;

    double machine_eps = APT::epsilon();

    ViewTypeA A("A",A_t?K:M,A_t?M:K);
    ViewTypeB B("B",B_t?N:K,B_t?K:N);
    ViewTypeC C("C",M,N);
    ViewTypeC C2("C",M,N);

    uint64_t seed = Kokkos::Impl::clock_tic();
    Kokkos::Random_XorShift64_Pool<execution_space> rand_pool(seed);

    // (SA 11 Dec 2019) Max (previously: 10) increased to detect the bug in Trilinos issue #6418
    Kokkos::fill_random(A,rand_pool, Kokkos::rand<typename Kokkos::Random_XorShift64_Pool<execution_space>::generator_type,ScalarA>::max());
    Kokkos::fill_random(B,rand_pool, Kokkos::rand<typename Kokkos::Random_XorShift64_Pool<execution_space>::generator_type,ScalarB>::max());
    Kokkos::fill_random(C,rand_pool, Kokkos::rand<typename Kokkos::Random_XorShift64_Pool<execution_space>::generator_type,ScalarC>::max());

    Kokkos::deep_copy(C2,C);
    Kokkos::fence();

    struct gemm_VanillaGEMM<ViewTypeA,ViewTypeB,ViewTypeC,execution_space> vgemm;
    vgemm.A_t = A_t; vgemm.B_t = B_t;
    vgemm.A_c = A_c; vgemm.B_c = B_c;
    vgemm.N = N;     vgemm.K = K;
    vgemm.A = A;     vgemm.B = B;
    vgemm.C = C2;
    vgemm.alpha = alpha;
    vgemm.beta = beta;

    Kokkos::parallel_for("KokkosBlas::Test::gemm_VanillaGEMM", Kokkos::TeamPolicy<execution_space>(M,Kokkos::AUTO,16), vgemm);

    KokkosBlas::gemm(TA,TB,alpha,A,B,beta,C);

    mag_type diff_C = 0;
    struct DiffGEMM<ViewTypeC,execution_space> diffgemm;
    diffgemm.N = N;
    diffgemm.C = C;
    diffgemm.C2 = C2;

    Kokkos::parallel_reduce("KokkosBlas::Test::DiffGEMM", Kokkos::TeamPolicy<execution_space>(M,Kokkos::AUTO,16), diffgemm, diff_C);

    if( N!=0 && M!=0) {
      int K_eff = (K == 0) ? 1 : K;
      double diff_C_average = diff_C/(N*M);
      // Expected Result: Random Walk in the least significant bit (i.e. ~ sqrt(K)*eps
      // eps scales with the total sum and has a factor in it for the accuracy of the operations ->
      // eps = K * 75 * machine_eps * 7
      double diff_C_expected = 1.0*sqrt(K_eff)*K_eff*75*machine_eps*7;

      if ( (diff_C_average >= 1.05*diff_C_expected ) ) {
        printf("Result: %e %e\n",diff_C_average,diff_C_expected);
      }
      EXPECT_TRUE( (diff_C_average < 1.05*diff_C_expected ) );
    }
  }
}

template<typename Scalar, typename Layout>
void test_gemm()
{
  typedef Kokkos::View<Scalar**, Layout, TestExecSpace> view_type_a;
  typedef Kokkos::View<Scalar**, Layout, TestExecSpace> view_type_b;
  typedef Kokkos::View<Scalar**, Layout, TestExecSpace> view_type_c;
  std::vector<const char*> modes = {"N", "T"};
  if(std::is_same<Scalar, Kokkos::complex<float>>::value || std::is_same<Scalar, Kokkos::complex<double>>::value)
    modes.push_back("C");
  Scalar alpha = 4.5;
  std::vector<Scalar> betas = {0.0, 3.0};
  for(Scalar beta : betas)
  {
    for(auto amode : modes)
    {
      for(auto bmode : modes)
      {
        Test::impl_test_gemm<view_type_a, view_type_b, view_type_c, TestExecSpace>(amode,bmode,0,0,0,alpha,beta);
        //BMK: N = 1 exercises the special GEMV code path in GEMM (currently, only for modes N/N)
        Test::impl_test_gemm<view_type_a, view_type_b, view_type_c, TestExecSpace>(amode,bmode,50,1,40,alpha,beta);
        //LBV: K = 0 exercise the quick return code path in GEMM
        Test::impl_test_gemm<view_type_a, view_type_b, view_type_c, TestExecSpace>(amode,bmode,20,14,0,alpha,beta);
        Test::impl_test_gemm<view_type_a, view_type_b, view_type_c, TestExecSpace>(amode,bmode,13,15,17,alpha,beta);
        Test::impl_test_gemm<view_type_a, view_type_b, view_type_c, TestExecSpace>(amode,bmode,179,15,211,alpha,beta);
        Test::impl_test_gemm<view_type_a, view_type_b, view_type_c, TestExecSpace>(amode,bmode,12,3071,517,alpha,beta);
      }
    }
  }
}

template<typename Scalar>
void test_gemm_enabled_layouts()
{
#if defined(KOKKOSKERNELS_INST_LAYOUTLEFT) || (!defined(KOKKOSKERNELS_ETI_ONLY) && !defined(KOKKOSKERNELS_IMPL_CHECK_ETI_CALLS))
  test_gemm<Scalar, Kokkos::LayoutLeft>();
#endif
#if defined(KOKKOSKERNELS_INST_LAYOUTRIGHT) || (!defined(KOKKOSKERNELS_ETI_ONLY) && !defined(KOKKOSKERNELS_IMPL_CHECK_ETI_CALLS))
  test_gemm<Scalar, Kokkos::LayoutRight>();
#endif
}

#if defined(KOKKOSKERNELS_INST_FLOAT) || (!defined(KOKKOSKERNELS_ETI_ONLY) && !defined(KOKKOSKERNELS_IMPL_CHECK_ETI_CALLS))
TEST_F( TestCategory, gemm_float ) {
  Kokkos::Profiling::pushRegion("KokkosBlas::Test::gemm_float");
  test_gemm_enabled_layouts<float>();
  Kokkos::Profiling::popRegion();
}
#endif

#if defined(KOKKOSKERNELS_INST_DOUBLE) || (!defined(KOKKOSKERNELS_ETI_ONLY) && !defined(KOKKOSKERNELS_IMPL_CHECK_ETI_CALLS))
TEST_F( TestCategory, gemm_double ) {
  Kokkos::Profiling::pushRegion("KokkosBlas::Test::gemm_double");
    test_gemm_enabled_layouts<double>();
  Kokkos::Profiling::popRegion();
}
#endif

#if defined(KOKKOSKERNELS_INST_COMPLEX_DOUBLE) || (!defined(KOKKOSKERNELS_ETI_ONLY) && !defined(KOKKOSKERNELS_IMPL_CHECK_ETI_CALLS))
TEST_F( TestCategory, gemm_complex_double ) {
  Kokkos::Profiling::pushRegion("KokkosBlas::Test::gemm_complex_double");
    test_gemm_enabled_layouts<Kokkos::complex<double>>();
  Kokkos::Profiling::popRegion();
}
#endif

#if defined(KOKKOSKERNELS_INST_COMPLEX_FLOAT) || (!defined(KOKKOSKERNELS_ETI_ONLY) && !defined(KOKKOSKERNELS_IMPL_CHECK_ETI_CALLS))
TEST_F( TestCategory, gemm_complex_float ) {
  Kokkos::Profiling::pushRegion("KokkosBlas::Test::gemm_complex_float");
    test_gemm_enabled_layouts<Kokkos::complex<float>>();
  Kokkos::Profiling::popRegion();
}
#endif

