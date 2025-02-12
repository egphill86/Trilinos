/*
//@HEADER
// ************************************************************************
//
//                        Kokkos v. 3.0
//       Copyright (2020) National Technology & Engineering
//               Solutions of Sandia, LLC (NTESS).
//
// Under the terms of Contract DE-NA0003525 with NTESS,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY NTESS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL NTESS OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Siva Rajamanickam (srajama@sandia.gov)
//
// ************************************************************************
//@HEADER
*/
#ifndef _KOKKOSKERNELS_SPARSEUTILS_HPP
#define _KOKKOSKERNELS_SPARSEUTILS_HPP
#include "Kokkos_Core.hpp"
#include "Kokkos_Atomic.hpp"
#include "Kokkos_Timer.hpp"
#include "KokkosKernels_SimpleUtils.hpp"
#include "KokkosKernels_IOUtils.hpp"
#include "KokkosKernels_ExecSpaceUtils.hpp"
#include <vector>
#include "KokkosKernels_PrintUtils.hpp"

#ifdef KOKKOSKERNELS_HAVE_PARALLEL_GNUSORT
#include<parallel/algorithm>
#endif

namespace KokkosKernels{


namespace Impl{

template <typename in_row_view_t,
          typename in_nnz_view_t,
		  typename in_val_view_t,
		  typename out_row_view_t,
		  typename out_nnz_view_t,
		  typename out_val_view_t>
void kk_create_blockcrs_formated_point_crsmatrix(
		int block_size,
	    size_t num_rows,
	    size_t num_cols,
	    in_row_view_t in_xadj,
	    in_nnz_view_t in_adj,
		in_val_view_t in_vals,


	    size_t &out_num_rows,
	    size_t &out_num_cols,
	    out_row_view_t &out_xadj,
	    out_nnz_view_t &out_adj,
		out_val_view_t &out_vals
	    ){

	typedef typename in_nnz_view_t::non_const_value_type lno_t;
	typedef typename in_row_view_t::non_const_value_type size_type;
	typedef typename in_val_view_t::non_const_value_type scalar_t;


    typename in_row_view_t::HostMirror hr = Kokkos::create_mirror_view (in_xadj);
    Kokkos::deep_copy (hr, in_xadj);
    typename in_nnz_view_t::HostMirror he = Kokkos::create_mirror_view (in_adj);
    Kokkos::deep_copy (he, in_adj);
    typename in_val_view_t::HostMirror hv = Kokkos::create_mirror_view (in_vals);
    Kokkos::deep_copy (hv, in_vals);

    out_num_rows = (num_rows / block_size) * block_size;
    if (num_rows % block_size) out_num_rows += block_size;

    out_num_cols = (num_cols / block_size) * block_size;
    if (num_cols % block_size) out_num_cols += block_size;


    std::vector<size_type> block_rows_xadj (out_num_rows + 1, 0);
    std::vector<lno_t>     block_adj; //(in_adj.extent(0), 0);
    std::vector<scalar_t>  block_vals;// (in_adj.extent(0), 0);

    std::vector<lno_t> block_columns (out_num_cols, 0);
    std::vector<scalar_t> block_accumulators (out_num_cols, 0);
    std::vector<bool> block_flags (out_num_cols, false);

    for (lno_t i = 0; i < lno_t(num_rows); i += block_size){
    	//std::cout << "row:" << i << std::endl;
    	lno_t outputrowsize = 0;

    	for (lno_t block_ind = 0; block_ind < block_size; ++block_ind){
    		lno_t row_ind = block_ind + i;
    		//std::cout << "\nrow_ind:" << row_ind << std::endl;
    		if (row_ind < lno_t(num_rows)){
    			size_type adj_begin = hr(row_ind);
    			size_type adj_end = hr(row_ind + 1);

    			lno_t row_size = adj_end - adj_begin;

    			for (lno_t col_ind = 0; col_ind < row_size; ++col_ind){

    				lno_t colid = he(col_ind + adj_begin);
    				//scalar_t colval = hv(col_ind);

    				lno_t block_column_start = (colid / block_size) * block_size;

    				for (lno_t kk = 0; kk < block_size; ++kk){
    					lno_t col_id_to_insert = block_column_start + kk;
        				//std::cout << colid << " " << block_column_start << " " << col_id_to_insert << " " << block_flags[col_id_to_insert] << " ## ";

    					if (block_flags[col_id_to_insert] == false) {
    						block_flags[col_id_to_insert] = true;
    						//block_adj[output_index + outputrowsize++] = col_id_to_insert;
    						//block_adj.push_back(col_id_to_insert);
    						block_columns[outputrowsize++] = col_id_to_insert;

    					}
    				}
    			}
    		}
    		else {

				lno_t colid = row_ind;
				//scalar_t colval = hv(col_ind);

				lno_t block_column_start = (colid / block_size) * block_size;

				for (lno_t kk = 0; kk < block_size; ++kk){
					lno_t col_id_to_insert = block_column_start + kk;
					if (block_flags[col_id_to_insert] == false) {
						block_flags[col_id_to_insert] = true;
						//block_adj[output_index + outputrowsize++] = col_id_to_insert;
						//block_adj.push_back(col_id_to_insert);
						block_columns[outputrowsize++] = col_id_to_insert;
					}
				}
    		}
    	}
		std::sort(block_columns.begin(), block_columns.begin() + outputrowsize);
		//std::cout << "\nrow:" << i << " outputrowsize:" << outputrowsize << std::endl;
    	for(lno_t kk = 0; kk < outputrowsize; ++kk){
    		block_flags[block_columns[kk]] = false;
    		//std::cout << block_columns[kk] << " ";
    	}
    	//std::cout << std::endl;

    	for (lno_t block_ind = 0; block_ind < block_size; ++block_ind){
    		lno_t row_ind = block_ind + i;
    		if (row_ind < lno_t(num_rows)){
    			size_type adj_begin = hr(row_ind);
    			size_type adj_end = hr(row_ind + 1);

    			lno_t row_size = adj_end - adj_begin;

    			for (lno_t col_ind = 0; col_ind < row_size; ++col_ind){
    				lno_t colid = he(col_ind + adj_begin);
    				scalar_t colval = hv(col_ind + adj_begin);
    				block_accumulators[colid] = colval;
    			}
    		}
    		else {
    			block_accumulators[row_ind] = 1;
    		}

        	for(lno_t kk = 0; kk < outputrowsize; ++kk){
        		lno_t outcol = block_columns[kk];
        		block_adj.push_back(outcol );
        		block_vals.push_back(block_accumulators[outcol]);
        		block_accumulators[outcol] = 0;
        	}
        	block_rows_xadj[row_ind + 1] = block_rows_xadj[row_ind] + outputrowsize;
    	}
    }


    out_xadj = out_row_view_t ("BlockedPointCRS XADJ", out_num_rows + 1);
    out_adj = out_nnz_view_t("BlockedPointCRS ADJ", block_adj.size());
    out_vals = out_val_view_t("BlockedPointCRS VALS", block_vals.size());


    typename out_row_view_t::HostMirror hor = Kokkos::create_mirror_view (out_xadj);
    typename out_nnz_view_t::HostMirror hoe = Kokkos::create_mirror_view (out_adj);
    typename out_val_view_t::HostMirror hov = Kokkos::create_mirror_view (out_vals);

    for (lno_t i = 0; i < lno_t(out_num_rows) + 1; ++i ){
    	hor(i) = block_rows_xadj[i];
    }

    size_type ne = block_adj.size();
    for (size_type i = 0; i < ne; ++i ){
    	hoe(i) = block_adj[i];
    }
    for (size_type i = 0; i < ne; ++i ){
    	hov(i) = block_vals[i];
    }

    Kokkos::deep_copy (out_xadj, hor);
    Kokkos::deep_copy (out_adj, hoe);
    Kokkos::deep_copy (out_vals, hov);
}

template <typename in_row_view_t,
          typename in_nnz_view_t,
		  typename in_val_view_t,
		  typename out_row_view_t,
		  typename out_nnz_view_t,
		  typename out_val_view_t>
void kk_create_blockcrs_from_blockcrs_formatted_point_crs(
		int block_size,
	    size_t num_rows,
	    size_t num_cols,
	    in_row_view_t in_xadj,
	    in_nnz_view_t in_adj,
		in_val_view_t in_vals,


	    size_t &out_num_rows,
	    size_t &out_num_cols,
	    out_row_view_t &out_xadj,
	    out_nnz_view_t &out_adj,
		out_val_view_t &out_vals
	    ){

    typename in_row_view_t::HostMirror hr = Kokkos::create_mirror_view (in_xadj);
    Kokkos::deep_copy (hr, in_xadj);
    typename in_nnz_view_t::HostMirror he = Kokkos::create_mirror_view (in_adj);
    Kokkos::deep_copy (he, in_adj);
    typename in_val_view_t::HostMirror hv = Kokkos::create_mirror_view (in_vals);
    Kokkos::deep_copy (hv, in_vals);


	out_num_rows = num_rows / block_size;
	out_num_cols = num_cols / block_size;


    out_xadj = out_row_view_t ("BlockedCRS XADJ", out_num_rows + 1);
    out_adj = out_nnz_view_t("BlockedCRS ADJ", in_adj.extent(0) / (block_size * block_size));
    out_vals = out_val_view_t("BlockedCRS VALS", in_vals.extent(0));


    typename out_row_view_t::HostMirror hor = Kokkos::create_mirror_view (out_xadj);
    typename out_nnz_view_t::HostMirror hoe = Kokkos::create_mirror_view (out_adj);
    typename out_val_view_t::HostMirror hov = Kokkos::create_mirror_view (out_vals);


	typedef typename in_nnz_view_t::non_const_value_type lno_t;
	typedef typename in_row_view_t::non_const_value_type size_type;
	//typedef typename in_val_view_t::non_const_value_type scalar_t;

    for(lno_t i = 0; i < lno_t(out_num_rows); ++i ){
    	hor(i) = hr(i * block_size) / (block_size * block_size);

    	size_type ib = hr(i * block_size);
    	size_type ie = hr(i * block_size + 1);

    	lno_t is = ie - ib;

    	size_type ob = hor(i);
    	//size_type oe = hr(i * block_size + 1) / block_size;
    	lno_t os = (ie - ib) / block_size;
    	lno_t write_index = 0;
    	for (lno_t j = 0; j < is; ++j){
    		lno_t e = he(ib + j);
    		if (e % block_size == 0){
    			hoe(ob + write_index++) = e / block_size;
    		}
    	}
    	if (write_index != os) {
    		std::cerr << "row:" << i << " expected size:" << os << " written size:" << write_index << std::endl;
    		exit(1);
    	}
    }
    hor(out_num_rows) = hr(out_num_rows * block_size) / (block_size * block_size);
    Kokkos::deep_copy (out_xadj, hor);
    Kokkos::deep_copy (out_adj, hoe);

    size_type ne = in_adj.extent(0);
    for(size_type i = 0; i < ne; ++i ){
    	hov(i) = hv(i);
    }
    Kokkos::deep_copy (out_vals, hov);
}

template <typename in_row_view_t,
          typename in_nnz_view_t,
          typename in_scalar_view_t,
          typename out_row_view_t,
          typename out_nnz_view_t,
          typename out_scalar_view_t,
          typename tempwork_row_view_t,
          typename MyExecSpace>
struct TransposeMatrix{

  struct CountTag{};
  struct FillTag{};

  typedef Kokkos::TeamPolicy<CountTag, MyExecSpace> team_count_policy_t ;
  typedef Kokkos::TeamPolicy<FillTag, MyExecSpace> team_fill_policy_t ;

  typedef typename team_count_policy_t::member_type team_count_member_t ;
  typedef typename team_fill_policy_t::member_type team_fill_member_t ;

  typedef typename in_nnz_view_t::non_const_value_type nnz_lno_t;
  typedef typename in_row_view_t::non_const_value_type size_type;

  typename in_nnz_view_t::non_const_value_type num_rows;
  typename in_nnz_view_t::non_const_value_type num_cols;
  in_row_view_t xadj;
  in_nnz_view_t adj;
  in_scalar_view_t vals;
  out_row_view_t t_xadj; //allocated
  out_nnz_view_t t_adj;  //allocated
  out_scalar_view_t t_vals;  //allocated
  tempwork_row_view_t tmp_txadj;
  bool transpose_values;
  nnz_lno_t team_work_size;

  TransposeMatrix(
      nnz_lno_t num_rows_,
      nnz_lno_t num_cols_,
      in_row_view_t xadj_,
      in_nnz_view_t adj_,
      in_scalar_view_t vals_,
      out_row_view_t t_xadj_,
      out_nnz_view_t t_adj_,
      out_scalar_view_t t_vals_,
      tempwork_row_view_t tmp_txadj_,
      bool transpose_values_,
      nnz_lno_t team_row_work_size_):
        num_rows(num_rows_), num_cols(num_cols_),
        xadj(xadj_), adj(adj_), vals(vals_),
        t_xadj(t_xadj_),  t_adj(t_adj_), t_vals(t_vals_),
        tmp_txadj(tmp_txadj_), transpose_values(transpose_values_), team_work_size(team_row_work_size_) {}

  KOKKOS_INLINE_FUNCTION
  void operator()(const CountTag&, const team_count_member_t & teamMember) const {

    const nnz_lno_t team_row_begin = teamMember.league_rank() * team_work_size;
    const nnz_lno_t team_row_end = KOKKOSKERNELS_MACRO_MIN(team_row_begin + team_work_size, num_rows);
    //TODO we dont need to go over rows
    //just go over nonzeroes.
    Kokkos::parallel_for(Kokkos::TeamThreadRange(teamMember,team_row_begin,team_row_end), [&] (const nnz_lno_t& row_index) {
      const size_type col_begin = xadj[row_index];
      const size_type col_end = xadj[row_index + 1];
      const nnz_lno_t left_work = col_end - col_begin;
      Kokkos::parallel_for(
          Kokkos::ThreadVectorRange(teamMember, left_work),
          [&] (nnz_lno_t i) {
        const size_type adjind = i + col_begin;
        const nnz_lno_t colIndex = adj[adjind];
        typedef typename std::remove_reference< decltype( t_xadj(0) ) >::type atomic_incr_type;
        Kokkos::atomic_fetch_add(&(t_xadj(colIndex)), atomic_incr_type(1));
      });
    });
  }

  KOKKOS_INLINE_FUNCTION
  void operator()(const FillTag&, const team_fill_member_t & teamMember) const {
    const nnz_lno_t team_row_begin = teamMember.league_rank() * team_work_size;
    const nnz_lno_t team_row_end = KOKKOSKERNELS_MACRO_MIN(team_row_begin + team_work_size, num_rows);


    Kokkos::parallel_for(Kokkos::TeamThreadRange(teamMember,team_row_begin,team_row_end), [&] (const nnz_lno_t& row_index) {
    //const nnz_lno_t teamsize = teamMember.team_size();
    //for (nnz_lno_t row_index = team_row_begin + teamMember.team_rank(); row_index < team_row_end; row_index += teamsize){
      const size_type col_begin = xadj[row_index];
      const size_type col_end = xadj[row_index + 1];
      const nnz_lno_t left_work = col_end - col_begin;
      Kokkos::parallel_for(
          Kokkos::ThreadVectorRange(teamMember, left_work),
          [&] (nnz_lno_t i) {
        const size_type adjind = i + col_begin;
        const nnz_lno_t colIndex = adj[adjind];
        typedef typename std::remove_reference< decltype( tmp_txadj(0) ) >::type atomic_incr_type;
        const size_type pos = Kokkos::atomic_fetch_add(&(tmp_txadj(colIndex)), atomic_incr_type(1));

        t_adj(pos) = row_index;
        if (transpose_values){
          t_vals(pos) = vals[adjind];
        }

      });
    //}
    });
  }
};

template <typename in_row_view_t,
          typename in_nnz_view_t,
          typename in_scalar_view_t,
          typename out_row_view_t,
          typename out_nnz_view_t,
          typename out_scalar_view_t,
          typename tempwork_row_view_t,
          typename MyExecSpace>
void transpose_matrix(
    typename in_nnz_view_t::non_const_value_type num_rows,
    typename in_nnz_view_t::non_const_value_type num_cols,
    in_row_view_t xadj,
    in_nnz_view_t adj,
    in_scalar_view_t vals,
    out_row_view_t t_xadj,    //pre-allocated -- initialized with 0
    out_nnz_view_t t_adj,     //pre-allocated -- no need for initialize
    out_scalar_view_t t_vals  //pre-allocated -- no need for initialize
    )
{
  //allocate some memory for work for row pointers
  tempwork_row_view_t tmp_row_view(Kokkos::view_alloc(Kokkos::WithoutInitializing, "tmp_row_view"), num_cols + 1);

  //create the functor for tranpose.
  typedef TransposeMatrix <
      in_row_view_t, in_nnz_view_t, in_scalar_view_t,
      out_row_view_t, out_nnz_view_t, out_scalar_view_t,
      tempwork_row_view_t, MyExecSpace>  TransposeFunctor_t;

  typedef typename TransposeFunctor_t::team_count_policy_t count_tp_t;
  typedef typename TransposeFunctor_t::team_fill_policy_t fill_tp_t;

  typename in_row_view_t::non_const_value_type nnz = adj.extent(0);

  //determine vector lanes per thread
  int thread_size = kk_get_suggested_vector_size(num_rows, nnz, kk_get_exec_space_type<MyExecSpace>());

  //determine threads per team
  int team_size = kk_get_suggested_team_size(thread_size, kk_get_exec_space_type<MyExecSpace>());

  TransposeFunctor_t tm ( num_rows, num_cols, xadj, adj, vals,
                          t_xadj, t_adj, t_vals,
                          tmp_row_view,
                          true,
                          team_size);

  Kokkos::parallel_for("KokkosKernels::Impl::transpose_matrix::S0", count_tp_t((num_rows + team_size - 1) / team_size, team_size, thread_size), tm);

  kk_exclusive_parallel_prefix_sum<out_row_view_t, MyExecSpace>(num_cols+1, t_xadj);

  Kokkos::deep_copy(tmp_row_view, t_xadj);

  Kokkos::parallel_for("KokkosKernels::Impl::transpose_matrix::S1", fill_tp_t((num_rows + team_size - 1) / team_size, team_size, thread_size), tm);

  MyExecSpace().fence();
}

template <typename crsMat_t>
crsMat_t transpose_matrix(const crsMat_t& A)
{
  //Allocate views and call the other version of transpose_matrix
  using c_rowmap_t = typename crsMat_t::row_map_type;
  using c_entries_t = typename crsMat_t::index_type;
  using c_values_t = typename crsMat_t::values_type;
  using rowmap_t = typename crsMat_t::row_map_type::non_const_type;
  using entries_t = typename crsMat_t::index_type::non_const_type;
  using values_t = typename crsMat_t::values_type::non_const_type;
  rowmap_t AT_rowmap("Transpose rowmap", A.numCols() + 1);
  entries_t AT_entries(
      Kokkos::view_alloc(Kokkos::WithoutInitializing, "Transpose entries"), A.nnz());
  values_t AT_values(
      Kokkos::view_alloc(Kokkos::WithoutInitializing, "Transpose values"), A.nnz());
  transpose_matrix<
    c_rowmap_t, c_entries_t, c_values_t,
    rowmap_t, entries_t, values_t,
    rowmap_t, typename crsMat_t::execution_space>(
        A.numRows(), A.numCols(),
        A.graph.row_map, A.graph.entries, A.values,
        AT_rowmap, AT_entries, AT_values);
  //And construct the transpose crsMat_t
  return crsMat_t("Transpose", A.numCols(), A.numRows(), A.nnz(), AT_values, AT_rowmap, AT_entries);
}

template <typename in_row_view_t,
          typename in_nnz_view_t,
          typename out_row_view_t,
          typename out_nnz_view_t,
          typename tempwork_row_view_t,
          typename MyExecSpace>
void transpose_graph(
    typename in_nnz_view_t::non_const_value_type num_rows,
    typename in_nnz_view_t::non_const_value_type num_cols,
    in_row_view_t xadj,
    in_nnz_view_t adj,
    out_row_view_t t_xadj, //pre-allocated -- initialized with 0
    out_nnz_view_t t_adj   //pre-allocated -- no need for initialize
    )
{
  //allocate some memory for work for row pointers
  tempwork_row_view_t tmp_row_view(Kokkos::view_alloc(Kokkos::WithoutInitializing, "tmp_row_view"), num_cols + 1);

  in_nnz_view_t tmp1;
  out_nnz_view_t tmp2;

  //create the functor for tranpose.
  typedef TransposeMatrix <
      in_row_view_t, in_nnz_view_t, in_nnz_view_t,
      out_row_view_t, out_nnz_view_t, out_nnz_view_t,
      tempwork_row_view_t, MyExecSpace>  TransposeFunctor_t;

  typedef typename TransposeFunctor_t::team_count_policy_t count_tp_t;
  typedef typename TransposeFunctor_t::team_fill_policy_t fill_tp_t;

  typename in_row_view_t::non_const_value_type nnz = adj.extent(0);

  //determine vector lanes per thread
  int thread_size = kk_get_suggested_vector_size(num_rows, nnz, kk_get_exec_space_type<MyExecSpace>());

  //determine threads per team
  int team_size = kk_get_suggested_team_size(thread_size, kk_get_exec_space_type<MyExecSpace>());

  TransposeFunctor_t tm ( num_rows, num_cols, xadj, adj, tmp1,
                          t_xadj, t_adj, tmp2,
                          tmp_row_view,
                          false,
                          team_size);

  Kokkos::parallel_for("KokkosKernels::Impl::transpose_graph::S0", count_tp_t((num_rows + team_size - 1) / team_size, team_size, thread_size), tm);

  kk_exclusive_parallel_prefix_sum<out_row_view_t, MyExecSpace>(num_cols+1, t_xadj);

  Kokkos::deep_copy(tmp_row_view, t_xadj);

  Kokkos::parallel_for("KokkosKernels::Impl::transpose_graph::S1", fill_tp_t((num_rows + team_size - 1) / team_size, team_size, thread_size), tm);

  MyExecSpace().fence();
}

template <typename forward_map_type, typename reverse_map_type>
struct Fill_Reverse_Scale_Functor{

  struct CountTag{};
  struct FillTag{};

  typedef struct CountTag CountTag;
  typedef struct FillTag FillTag;


  typedef typename forward_map_type::value_type forward_type;
  typedef typename reverse_map_type::value_type reverse_type;
  forward_map_type forward_map;
  reverse_map_type reverse_map_xadj;
  reverse_map_type reverse_map_adj;

  const reverse_type multiply_shift_for_scale;
  const reverse_type division_shift_for_bucket;


  Fill_Reverse_Scale_Functor(
      forward_map_type forward_map_,
      reverse_map_type reverse_map_xadj_,
      reverse_map_type reverse_map_adj_,
      reverse_type multiply_shift_for_scale_,
      reverse_type division_shift_for_bucket_):
        forward_map(forward_map_), reverse_map_xadj(reverse_map_xadj_), reverse_map_adj(reverse_map_adj_),
        multiply_shift_for_scale(multiply_shift_for_scale_),
        division_shift_for_bucket(division_shift_for_bucket_){}

  KOKKOS_INLINE_FUNCTION
  void operator()(const CountTag&, const size_t &ii) const {
    forward_type fm = forward_map[ii];
    fm = fm << multiply_shift_for_scale;
    fm += ii >> division_shift_for_bucket;
    typedef typename std::remove_reference< decltype( reverse_map_xadj(0) ) >::type atomic_incr_type;
    Kokkos::atomic_fetch_add( &(reverse_map_xadj(fm)), atomic_incr_type(1));
  }

  KOKKOS_INLINE_FUNCTION
  void operator()(const FillTag&, const size_t &ii) const {
    forward_type fm = forward_map[ii];

    fm = fm << multiply_shift_for_scale;
    fm += ii >> division_shift_for_bucket;
    typedef typename std::remove_reference< decltype( reverse_map_xadj(0) ) >::type atomic_incr_type;
    const reverse_type future_index = Kokkos::atomic_fetch_add( &(reverse_map_xadj(fm) ), atomic_incr_type(1));
    reverse_map_adj(future_index) = ii;
  }
};


template <typename from_view_t, typename to_view_t>
struct StridedCopy1{
  const from_view_t from;
  to_view_t to;
  const size_t stride;
  StridedCopy1(
      const from_view_t from_,
      to_view_t to_,
      size_t stride_):from(from_), to (to_), stride(stride_){}


  KOKKOS_INLINE_FUNCTION
  void operator()(const size_t &ii) const {
    to[ii] = from[(ii) * stride];
  }
};

template <typename forward_map_type, typename reverse_map_type>
struct Reverse_Map_Functor{

  struct CountTag{};
  struct FillTag{};

  typedef struct CountTag CountTag;
  typedef struct FillTag FillTag;


  typedef typename forward_map_type::value_type forward_type;
  typedef typename reverse_map_type::value_type reverse_type;
  forward_map_type forward_map;
  reverse_map_type reverse_map_xadj;
  reverse_map_type reverse_map_adj;


  Reverse_Map_Functor(
      forward_map_type forward_map_,
      reverse_map_type reverse_map_xadj_,
      reverse_map_type reverse_map_adj_):
        forward_map(forward_map_), reverse_map_xadj(reverse_map_xadj_), reverse_map_adj(reverse_map_adj_){}

  KOKKOS_INLINE_FUNCTION
  void operator()(const CountTag&, const size_t &ii) const {
    forward_type fm = forward_map[ii];
    typedef typename std::remove_reference< decltype( reverse_map_xadj(0) ) >::type atomic_incr_type;
    Kokkos::atomic_fetch_add( &(reverse_map_xadj(fm)), atomic_incr_type(1));
  }

  KOKKOS_INLINE_FUNCTION
  void operator()(const FillTag&, const size_t &ii) const {
    forward_type c = forward_map[ii];
    typedef typename std::remove_reference< decltype( reverse_map_xadj(0) ) >::type atomic_incr_type;
    const reverse_type future_index = Kokkos::atomic_fetch_add( &(reverse_map_xadj(c)), atomic_incr_type(1));
    reverse_map_adj(future_index) = ii;
  }
};


/**
 * \brief Utility function to obtain a reverse map given a map.
 * Input is a map with the number of elements within the map.
 * forward_map[c] = i, where c is a forward element and forward_map has a size of num_forward_elements.
 * i is the value that c is mapped in the forward map, and the range of that is num_reverse_elements.
 * Output is the reverse_map_xadj and reverse_map_adj such that,
 * all c, forward_map[c] = i, will appear in  reverse_map_adj[ reverse_map_xadj[i]: reverse_map_xadj[i+1])
 * \param: num_forward_elements: the number of elements in the forward map, the size of the forward map.
 * \param: num_reverse_elements: the number of elements that forward map is mapped to. It is the value of max i.
 * \param: forward_map: input forward_map, where forward_map[c] = i.
 * \param: reverse_map_xadj: reverse map xadj, that is it will hold the beginning and
 * end indices on reverse_map_adj such that all values mapped to i will be [ reverse_map_xadj[i]: reverse_map_xadj[i+1])
 * its size will be num_reverse_elements + 1. NO NEED TO INITIALIZE.
 * \param: reverse_map_adj: reverse map adj, holds the values of reverse maps. Its size is num_forward_elements.
 *
 */
template <typename forward_array_type, typename reverse_array_type, typename MyExecSpace>
void kk_create_reverse_map(
    const typename reverse_array_type::value_type &num_forward_elements, //num_vertices
    const typename forward_array_type::value_type &num_reverse_elements, //num_colors

    const forward_array_type &forward_map, //vertex to colors
    const reverse_array_type &reverse_map_xadj, // colors to vertex xadj
    const reverse_array_type &reverse_map_adj){ //colros to vertex adj

  typedef typename reverse_array_type::value_type lno_t;
  typedef typename forward_array_type::value_type reverse_lno_t;

  const lno_t  MINIMUM_TO_ATOMIC = 128;

  //typedef Kokkos::TeamPolicy<CountTag, MyExecSpace> team_count_policy_t ;
  //typedef Kokkos::TeamPolicy<FillTag, MyExecSpace> team_fill_policy_t ;

  typedef Kokkos::RangePolicy<MyExecSpace> my_exec_space;

  //IF There are very few reverse elements, atomics are likely to create contention.
  if (num_reverse_elements < MINIMUM_TO_ATOMIC){
    const lno_t scale_size = 1024;
    const lno_t multiply_shift_for_scale = 10;

    //there will be 1024 buckets
    const lno_t division_shift_for_bucket =
          lno_t (ceil(log(double (num_forward_elements) / scale_size)/log(2)));

    //coloring indices are base-1. we end up using not using element 1.
    const reverse_lno_t tmp_reverse_size =
        (num_reverse_elements + 1) << multiply_shift_for_scale;

    typename reverse_array_type::non_const_type
        tmp_color_xadj ("TMP_REVERSE_XADJ", tmp_reverse_size + 1);

    typedef Fill_Reverse_Scale_Functor<forward_array_type, reverse_array_type> frsf;
    typedef typename frsf::CountTag cnt_tag;
    typedef typename frsf::FillTag fill_tag;
    typedef Kokkos::RangePolicy<cnt_tag, MyExecSpace> my_cnt_exec_space;
    typedef Kokkos::RangePolicy<fill_tag, MyExecSpace> my_fill_exec_space;

    frsf frm (forward_map, tmp_color_xadj, reverse_map_adj,
            multiply_shift_for_scale, division_shift_for_bucket);

    Kokkos::parallel_for ("KokkosKernels::Common::CreateReverseMap::NonAtomic::S0", my_cnt_exec_space (0, num_forward_elements) , frm);
    MyExecSpace().fence();


    //kk_inclusive_parallel_prefix_sum<reverse_array_type, MyExecSpace>(tmp_reverse_size + 1, tmp_color_xadj);
    kk_exclusive_parallel_prefix_sum<reverse_array_type, MyExecSpace>
      (tmp_reverse_size + 1, tmp_color_xadj);
    MyExecSpace().fence();

    Kokkos::parallel_for ("KokkosKernels::Common::CreateReverseMap::NonAtomic::S1",
        my_exec_space (0, num_reverse_elements + 1) ,
        StridedCopy1<reverse_array_type, reverse_array_type>
          (tmp_color_xadj, reverse_map_xadj, scale_size));
    MyExecSpace().fence();
    Kokkos::parallel_for ("KokkosKernels::Common::CreateReverseMap::NonAtomic::S2",my_fill_exec_space (0, num_forward_elements) , frm);
    MyExecSpace().fence();
  }
  else
  //atomic implementation.
  {
    reverse_array_type tmp_color_xadj ("TMP_REVERSE_XADJ", num_reverse_elements + 1);

    typedef Reverse_Map_Functor<forward_array_type, reverse_array_type> rmp_functor_type;
    typedef typename rmp_functor_type::CountTag cnt_tag;
    typedef typename rmp_functor_type::FillTag fill_tag;
    typedef Kokkos::RangePolicy<cnt_tag, MyExecSpace> my_cnt_exec_space;
    typedef Kokkos::RangePolicy<fill_tag, MyExecSpace> my_fill_exec_space;

    rmp_functor_type frm (forward_map, tmp_color_xadj, reverse_map_adj);

    Kokkos::parallel_for ("KokkosKernels::Common::CreateReverseMap::Atomic::S0", my_cnt_exec_space (0, num_forward_elements) , frm);
    MyExecSpace().fence();

    //kk_inclusive_parallel_prefix_sum<reverse_array_type, MyExecSpace>(num_reverse_elements + 1, reverse_map_xadj);
    kk_exclusive_parallel_prefix_sum<reverse_array_type, MyExecSpace>
      (num_reverse_elements + 1, tmp_color_xadj);
    MyExecSpace().fence();

    Kokkos::deep_copy (reverse_map_xadj, tmp_color_xadj);
    MyExecSpace().fence();

    Kokkos::parallel_for ("KokkosKernels::Common::CreateReverseMap::Atomic::S1", my_fill_exec_space (0, num_forward_elements) , frm);
    MyExecSpace().fence();
  }
}

template <typename in_row_view_t, typename in_nnz_view_t,  typename in_color_view_t,
          typename team_member>
struct ColorChecker{



  typedef typename in_row_view_t::value_type size_type;
  typedef typename in_nnz_view_t::value_type lno_t;
  typedef typename in_color_view_t::value_type color_t;
  lno_t num_rows;
  in_row_view_t xadj;
  in_nnz_view_t adj;
  in_color_view_t color_view;
  lno_t team_row_chunk_size;



  ColorChecker(
      lno_t num_rows_,
      in_row_view_t xadj_,
      in_nnz_view_t adj_,
      in_color_view_t color_view_,
      lno_t chunk_size):
        num_rows(num_rows_),
        xadj(xadj_), adj(adj_), color_view(color_view_),
        team_row_chunk_size(chunk_size){}

  KOKKOS_INLINE_FUNCTION
  void operator()(const team_member & teamMember, size_t &num_conflicts) const {
    //get the range of rows for team.
    const lno_t team_row_begin = teamMember.league_rank() * team_row_chunk_size;
    const lno_t team_row_end = KOKKOSKERNELS_MACRO_MIN(team_row_begin + team_row_chunk_size, num_rows);

    size_t nf = 0;
    Kokkos::parallel_reduce(Kokkos::TeamThreadRange(teamMember, team_row_begin, team_row_end), [&] (const lno_t& row_index, size_t &team_num_conf)
    {

      color_t my_color = color_view(row_index);
      const size_type col_begin = xadj[row_index];
      const size_type col_end = xadj[row_index + 1];
      const lno_t left_work = col_end - col_begin;

      size_t conf1= 0;
      Kokkos::parallel_reduce(
          Kokkos::ThreadVectorRange(teamMember, left_work),
          [&] (lno_t i, size_t & valueToUpdate) {
        const size_type adjind = i + col_begin;
        const lno_t colIndex = adj[adjind];
        if (colIndex != row_index){
          color_t second_color = color_view(colIndex);
          if (second_color == my_color)
            valueToUpdate += 1;
        }
      },
      conf1);
      team_num_conf += conf1;
    }, nf);
    num_conflicts += nf;
  }
};

/**
 * \brief given a graph and a coloring function returns true or false if distance-1 coloring is valid or not.
 * \param num_rows: num rows in input graph
 * \param num_cols: num cols in input graph
 * \param xadj: row pointers of the input graph
 * \param adj: column indices of the input graph
 * \param t_xadj: output, the row indices of the output graph. MUST BE INITIALIZED WITH ZEROES.

 * \param vector_size: suggested vector size, optional. if -1, kernel will decide.
 * \param suggested_team_size: suggested team size, optional. if -1, kernel will decide.
 * \param team_work_chunk_size: suggested work size of a team, optional. if -1, kernel will decide.
 * \param use_dynamic_scheduling: whether to use dynamic scheduling. Default is true.
 */
template <typename in_row_view_t,
          typename in_nnz_view_t,
          typename in_color_view_t,
          typename MyExecSpace>
inline size_t kk_is_d1_coloring_valid(
    typename in_nnz_view_t::non_const_value_type num_rows,
    typename in_nnz_view_t::non_const_value_type /*num_cols*/,
    in_row_view_t xadj,
    in_nnz_view_t adj,
    in_color_view_t v_colors
    ){
  ExecSpaceType my_exec_space = kk_get_exec_space_type<MyExecSpace>();
  int vector_size = kk_get_suggested_vector_size(num_rows, adj.extent(0), my_exec_space);
  int suggested_team_size = kk_get_suggested_team_size(vector_size, my_exec_space);;
  typename in_nnz_view_t::non_const_value_type team_work_chunk_size = suggested_team_size;
  typedef Kokkos::TeamPolicy<MyExecSpace, Kokkos::Schedule<Kokkos::Dynamic> > dynamic_team_policy ;
  typedef typename dynamic_team_policy::member_type team_member_t ;

  struct ColorChecker <in_row_view_t, in_nnz_view_t, in_color_view_t, team_member_t>  cc(num_rows, xadj, adj, v_colors, team_work_chunk_size);
  size_t num_conf = 0;
  Kokkos::parallel_reduce( "KokkosKernels::Common::IsD1ColoringValid", dynamic_team_policy(num_rows / team_work_chunk_size + 1 ,
      suggested_team_size, vector_size), cc, num_conf);

  MyExecSpace().fence();
  return num_conf;
}

template<typename Reducer, typename ordinal_t, typename rowmap_t>
struct MinMaxDegreeFunctor
{
  using ReducerVal = typename Reducer::value_type;
  MinMaxDegreeFunctor(const rowmap_t& rowmap_)
    : rowmap(rowmap_) {}
  KOKKOS_INLINE_FUNCTION void operator()(ordinal_t i, ReducerVal& lminmax) const
  {
    ordinal_t deg = rowmap(i + 1) - rowmap(i);
    if(deg < lminmax.min_val)
      lminmax.min_val = deg;
    if(deg > lminmax.max_val)
      lminmax.max_val = deg;
  }
  rowmap_t rowmap;
};

template<typename Reducer, typename ordinal_t, typename rowmap_t>
struct MaxDegreeFunctor
{
  using ReducerVal = typename Reducer::value_type;
  MaxDegreeFunctor(const rowmap_t& rowmap_)
    : rowmap(rowmap_) {}
  KOKKOS_INLINE_FUNCTION void operator()(ordinal_t i, ReducerVal& lmax) const
  {
    ordinal_t deg = rowmap(i + 1) - rowmap(i);
    if(deg > lmax)
      lmax = deg;
  }
  rowmap_t rowmap;
};

template<typename device_t, typename ordinal_t, typename rowmap_t>
ordinal_t graph_max_degree(const rowmap_t& rowmap)
{
  using Reducer = Kokkos::Max<ordinal_t>;
  ordinal_t nrows = rowmap.extent(0);
  if(nrows)
    nrows--;
  if(nrows == 0)
    return 0;
  ordinal_t val;
  Kokkos::parallel_reduce(
      Kokkos::RangePolicy<typename device_t::execution_space>(0, nrows),
      MaxDegreeFunctor<Reducer, ordinal_t, rowmap_t>(rowmap),
      Reducer(val));
  return val;
}

template<typename device_t, typename ordinal_t, typename rowmap_t>
void graph_min_max_degree(const rowmap_t& rowmap, ordinal_t& min_degree, ordinal_t& max_degree)
{
  using Reducer = Kokkos::MinMax<ordinal_t>;
  ordinal_t nrows = rowmap.extent(0);
  if(nrows)
    nrows--;
  if(nrows == 0)
  {
    min_degree = 0;
    max_degree = 0;
    return;
  }
  typename Reducer::value_type result;
  Kokkos::parallel_reduce(
      Kokkos::RangePolicy<typename device_t::execution_space>(0, nrows),
      MinMaxDegreeFunctor<Reducer, ordinal_t, rowmap_t>(rowmap),
      Reducer(result));
  min_degree = result.min_val;
  max_degree = result.max_val;
}

/*
template <typename in_row_view_t,
          typename in_nnz_view_t,
          typename out_nnz_view_t,
          typename MyExecSpace>
struct IncidenceMatrix{

  struct FillTag{};

  typedef struct FillTag FillTag;

  typedef Kokkos::TeamPolicy<FillTag, MyExecSpace> team_fill_policy_t ;
  typedef Kokkos::TeamPolicy<FillTag, MyExecSpace, Kokkos::Schedule<Kokkos::Dynamic> > dynamic_team_fill_policy_t ;
  typedef typename team_fill_policy_t::member_type team_fill_member_t ;

  typedef typename in_nnz_view_t::non_const_value_type nnz_lno_t;
  typedef typename in_row_view_t::non_const_value_type size_type;


  typename in_nnz_view_t::non_const_value_type num_rows;
  in_row_view_t xadj;
  in_nnz_view_t adj;
  out_nnz_view_t t_adj;  //allocated
  typename in_row_view_t::non_const_type tmp_txadj;
  nnz_lno_t team_work_size;

  IncidenceMatrix(
      nnz_lno_t num_rows_,
      in_row_view_t xadj_,
      in_nnz_view_t adj_,
      out_nnz_view_t t_adj_,
      typename in_row_view_t::non_const_type tmp_txadj_,
      nnz_lno_t team_row_work_size_):
        num_rows(num_rows_),
        xadj(xadj_), adj(adj_),
        t_adj(t_adj_),
        tmp_txadj(tmp_txadj_), team_work_size(team_row_work_size_) {}


  KOKKOS_INLINE_FUNCTION
  void operator()(const FillTag&, const team_fill_member_t & teamMember) const {
    const nnz_lno_t team_row_begin = teamMember.league_rank() * team_work_size;
    const nnz_lno_t team_row_end = KOKKOSKERNELS_MACRO_MIN(team_row_begin + team_work_size, num_rows);


    Kokkos::parallel_for(Kokkos::TeamThreadRange(teamMember,team_row_begin,team_row_end), [&] (const nnz_lno_t& row_index) {
      const size_type col_begin = xadj[row_index];
      const size_type col_end = xadj[row_index + 1];
      const nnz_lno_t left_work = col_end - col_begin;
      Kokkos::parallel_for(
          Kokkos::ThreadVectorRange(teamMember, left_work),
          [&] (nnz_lno_t i) {
        const size_type adjind = i + col_begin;
        const nnz_lno_t colIndex = adj[adjind];
        if (row_index < colIndex){

          const size_type pos = Kokkos::atomic_fetch_add(&(tmp_txadj(colIndex)),1);
          t_adj(adjind) = adjind;
          t_adj(pos) = adjind;
        }
      });
    //}
    });
  }
};
*/
/**
 * \brief function returns transpose of the given graph.
 * \param num_rows: num rows in input graph
 * \param num_cols: num cols in input graph
 * \param xadj: row pointers of the input graph
 * \param adj: column indices of the input graph
 * \param t_xadj: output, the row indices of the output graph. MUST BE INITIALIZED WITH ZEROES.
 * \param t_adj: output, column indices. No need for initializations.
 * \param vector_size: suggested vector size, optional. if -1, kernel will decide.
 * \param suggested_team_size: suggested team size, optional. if -1, kernel will decide.
 * \param team_work_chunk_size: suggested work size of a team, optional. if -1, kernel will decide.
 * \param use_dynamic_scheduling: whether to use dynamic scheduling. Default is true.
 */
/*
template <typename in_row_view_t,
          typename in_nnz_view_t,
          typename out_nnz_view_t,
          typename MyExecSpace>
inline void kk_create_incidence_matrix(
    typename in_nnz_view_t::non_const_value_type num_rows,
    in_row_view_t xadj,
    in_nnz_view_t adj,
    out_nnz_view_t i_adj,  //pre-allocated -- no need for initialize -- size is same as adj
    int vector_size = -1,
    int suggested_team_size = -1,
    typename in_nnz_view_t::non_const_value_type team_work_chunk_size = -1,
    bool use_dynamic_scheduling = true
    ){


  typedef typename in_row_view_t::non_const_type tmp_row_view_t;
  //allocate some memory for work for row pointers
  tmp_row_view_t tmp_row_view(Kokkos::view_alloc(Kokkos::WithoutInitializing, "tmp_row_view"), num_rows + 1);

  Kokkos::deep_copy(tmp_row_view, xadj);

  in_nnz_view_t tmp1;
  out_nnz_view_t tmp2;

  //create the functor for tranpose.
  typedef IncidenceMatrix <
      in_row_view_t, in_nnz_view_t, in_nnz_view_t,
      out_nnz_view_t, MyExecSpace>  IncidenceMatrix_Functor_t;

  IncidenceMatrix_Functor_t tm ( num_rows, xadj, adj,
                                t_adj, tmp_row_view,
                                false,
                                team_work_chunk_size);


  typedef typename IncidenceMatrix_Functor_t::team_fill_policy_t fill_tp_t;
  typedef typename IncidenceMatrix_Functor_t::dynamic_team_fill_policy_t d_fill_tp_t;

  typename in_row_view_t::non_const_value_type nnz = adj.extent(0);

  //set the vector size, if not suggested.
  if (vector_size == -1)
    vector_size = kk_get_suggested_vector_size(num_rows, nnz, kk_get_exec_space_type<MyExecSpace>());

  //set the team size, if not suggested.
  if (suggested_team_size == -1)
    suggested_team_size = kk_get_suggested_team_size(vector_size, kk_get_exec_space_type<MyExecSpace>());

  //set the chunk size, if not suggested.
  if (team_work_chunk_size == -1)
    team_work_chunk_size = suggested_team_size;



  if (use_dynamic_scheduling){
    Kokkos::parallel_for(  fill_tp_t(num_rows  / team_work_chunk_size + 1 , suggested_team_size, vector_size), tm);
  }
  else {
    Kokkos::parallel_for(  d_fill_tp_t(num_rows  / team_work_chunk_size + 1 , suggested_team_size, vector_size), tm);
  }
  MyExecSpace().fence();

}
*/

template <typename size_type, typename lno_t>
void kk_get_lower_triangle_count_sequential(
    const lno_t nv,
    const size_type *in_xadj,
    const lno_t *in_adj,
    size_type *out_xadj,
    const lno_t *new_indices = NULL
    ){
  for (lno_t i = 0; i < nv; ++i){
    lno_t row_index = i;

    if (new_indices) row_index = new_indices[i];

    out_xadj[i] = 0;
    size_type begin = in_xadj[i];
    lno_t rowsize = in_xadj[i + 1] - begin;

    for (lno_t j = 0; j < rowsize; ++j){
      lno_t col = in_adj[j + begin];
      lno_t col_index = col;
      if (new_indices) col_index = new_indices[col];

      if (row_index > col_index){
        ++out_xadj[i];
      }
    }
  }
}



template <typename size_type,
          typename lno_t,
          typename ExecutionSpace,
          typename scalar_t = double>
struct LowerTriangularMatrix{

  struct CountTag{};
  struct FillTag{};

  typedef struct CountTag CountTag;
  typedef struct FillTag FillTag;

  typedef Kokkos::TeamPolicy<CountTag, ExecutionSpace> team_count_policy_t ;
  typedef Kokkos::TeamPolicy<FillTag, ExecutionSpace> team_fill_policy_t ;

  typedef Kokkos::TeamPolicy<CountTag, ExecutionSpace, Kokkos::Schedule<Kokkos::Dynamic> > dynamic_team_count_policy_t ;
  typedef Kokkos::TeamPolicy<FillTag, ExecutionSpace, Kokkos::Schedule<Kokkos::Dynamic> > dynamic_team_fill_policy_t ;


  typedef typename team_count_policy_t::member_type team_count_member_t ;
  typedef typename team_fill_policy_t::member_type team_fill_member_t ;


  lno_t num_rows;
  const size_type * xadj;
  const lno_t * adj;
  const scalar_t *in_vals;
  const lno_t *permutation;

  size_type * t_xadj; //allocated
  lno_t * t_adj;  //allocated
  scalar_t *t_vals;

  const lno_t team_work_size;
  const ExecSpaceType exec_space;
  const bool is_lower;

  LowerTriangularMatrix(
      const lno_t num_rows_,
      const size_type * xadj_,
      const lno_t * adj_,
      const scalar_t *in_vals_,
      const lno_t *permutation_,
      size_type * t_xadj_,
      lno_t * t_adj_,
      scalar_t *out_vals_,
      const lno_t team_row_work_size_,
      bool is_lower_ = true):
        num_rows(num_rows_),
        xadj(xadj_), adj(adj_), in_vals (in_vals_),permutation(permutation_),
        t_xadj(t_xadj_),  t_adj(t_adj_), t_vals(out_vals_),
        team_work_size(team_row_work_size_), exec_space (kk_get_exec_space_type<ExecutionSpace>()), is_lower(is_lower_) {}

  KOKKOS_INLINE_FUNCTION
  void operator()(const CountTag&, const team_count_member_t & teamMember) const {

    const lno_t team_row_begin = teamMember.league_rank() * team_work_size;
    const lno_t team_row_end = KOKKOSKERNELS_MACRO_MIN(team_row_begin + team_work_size, num_rows);

    Kokkos::parallel_for(Kokkos::TeamThreadRange(teamMember,team_row_begin,team_row_end), [&] (const lno_t& row_index) {
      lno_t row_perm = row_index;
      if (permutation != NULL){
        row_perm = permutation[row_perm];
      }

      const size_type col_begin = xadj[row_index];
      const size_type col_end = xadj[row_index + 1];
      const lno_t left_work = col_end - col_begin;
      lno_t lower_row_size = 0;
      Kokkos::parallel_reduce(
          Kokkos::ThreadVectorRange(teamMember, left_work),
          [&] (lno_t i, lno_t &rowsize_) {
        const size_type adjind = i + col_begin;
        lno_t colIndex = adj[adjind];
        if (permutation != NULL){
          colIndex = permutation[colIndex];
        }
        if (is_lower){
          if (row_perm > colIndex){
            rowsize_ += 1;
          }
        }
        else {
          if (row_perm < colIndex){
            rowsize_ += 1;
          }
        }
      }, lower_row_size);

      t_xadj[row_index] = lower_row_size;
    });
  }

  KOKKOS_INLINE_FUNCTION
  void operator()(const FillTag&, const team_fill_member_t & teamMember) const {

    const lno_t team_row_begin = teamMember.league_rank() * team_work_size;
    const lno_t team_row_end = KOKKOSKERNELS_MACRO_MIN(team_row_begin + team_work_size, num_rows);

    Kokkos::parallel_for(Kokkos::TeamThreadRange(teamMember,team_row_begin,team_row_end), [&] (const lno_t& row_index) {
      lno_t row_perm = row_index;
      if (permutation != NULL){
        row_perm = permutation[row_perm];
      }

      const size_type col_begin = xadj[row_index];
      const size_type col_end = xadj[row_index + 1];
      const lno_t read_left_work = col_end - col_begin;


      const size_type write_begin = t_xadj[row_index];
      const size_type write_end = t_xadj[row_index + 1];
      const lno_t write_left_work = write_end - write_begin;

      //TODO: Write GPU (vector-level) version here:
      /*
      if(kk_is_gpu_exec_space<ExecutionSpace>())
      {
        Kokkos::parallel_for(
            Kokkos::ThreadVectorRange(teamMember, read_left_work),
            [&] (lno_t i) {
          const size_type adjind = i + col_begin;
          const lno_t colIndex = adj[adjind];
        });
      }
      else
      ...
      */

      for (lno_t r = 0 , w = 0; r <  read_left_work && w < write_left_work; ++r){
        const size_type adjind = r + col_begin;
        const lno_t colIndex = adj[adjind];
        lno_t colperm = colIndex;
        if (permutation != NULL){
          colperm = permutation[colIndex];
        }
        if (is_lower){
          if (row_perm > colperm){
            if (in_vals != NULL){
              t_vals[write_begin + w] = in_vals[adjind];
            }
            t_adj[write_begin + w++] = colIndex;
          }
        }
        else {
          if (row_perm < colperm){
            if (in_vals != NULL){
              t_vals[write_begin + w] = in_vals[adjind];
            }
            t_adj[write_begin + w++] = colIndex;
          }
        }


      }
    });
  }
};
template <typename size_type, typename lno_t, typename ExecutionSpace>
void kk_get_lower_triangle_count_parallel(
    const lno_t nv,
    const size_type ne,
    const size_type *in_xadj,
    const lno_t *in_adj,
    size_type *out_xadj,
    const lno_t *new_indices = NULL,
    bool use_dynamic_scheduling = false,
    int chunksize = 4,
    bool is_lower = true
    ){


  const int vector_size = kk_get_suggested_vector_size(nv, ne, kk_get_exec_space_type<ExecutionSpace>());
  const int suggested_team_size = kk_get_suggested_team_size(vector_size, kk_get_exec_space_type<ExecutionSpace>());
  const int team_work_chunk_size = suggested_team_size * chunksize;
  typedef LowerTriangularMatrix<size_type, lno_t, ExecutionSpace> ltm_t;

  ltm_t ltm (
      nv,
      in_xadj,
      in_adj, NULL,
      new_indices,
      out_xadj,
      NULL, NULL,
      team_work_chunk_size,
      is_lower);


  typedef typename ltm_t::team_count_policy_t count_tp_t;
  typedef typename ltm_t::dynamic_team_count_policy_t d_count_tp_t;


  if (use_dynamic_scheduling){
    Kokkos::parallel_for( "KokkosKernels::Common::GetLowerTriangleCount::DynamicSchedule",  d_count_tp_t(nv  / team_work_chunk_size + 1 , suggested_team_size, vector_size), ltm);
  }
  else {
    Kokkos::parallel_for( "KokkosKernels::Common::GetLowerTriangleCount::StaticSchedule", count_tp_t(nv  / team_work_chunk_size + 1 , suggested_team_size, vector_size), ltm);
  }
  ExecutionSpace().fence();
}






template <typename size_type, typename lno_t>
void kk_sort_by_row_size_sequential(
    const lno_t nv,
    const size_type *in_xadj,
    lno_t *new_indices,
    int sort_decreasing_order = 1){


  std::vector<lno_t> begins (nv);
  std::vector<lno_t> nexts (nv);
  for (lno_t i = 0; i < nv; ++i){
    nexts[i] = begins[i] = -1;
  }



  for (lno_t i = 0; i < nv; ++i){
    lno_t row_size = in_xadj[i+1] - in_xadj[i];
    nexts [i] = begins[row_size];
    begins[row_size] = i;
  }
  if (sort_decreasing_order == 1){
    lno_t new_index = nv;
    const lno_t row_end = -1;
    for (lno_t i = 0; i < nv ; ++i){
      lno_t row = begins[i];
      while (row != row_end){
        new_indices[row] = --new_index;
        row = nexts[row];
      }
    }
  }
  else if (sort_decreasing_order == 2){
    lno_t new_index_top = nv;
    lno_t new_index_bottom = 0;
    const lno_t row_end = -1;
    bool is_even = true;
    for (lno_t i = nv - 1; ; --i){
      lno_t row = begins[i];
      while (row != row_end){
        if (is_even){
          new_indices[row] = --new_index_top;
        }
        else {
          new_indices[row] = new_index_bottom++;
        }
        is_even = !is_even;
        row = nexts[row];
      }
      if (i == 0) break;
    }
  }
  else {
    lno_t new_index = 0;
    const lno_t row_end = -1;
    for (lno_t i = 0; i < nv ; ++i){
      lno_t row = begins[i];
      while (row != row_end){
        new_indices[row] = new_index++;
        row = nexts[row];
      }
    }
  }
}
#ifdef KOKKOSKERNELS_HAVE_PARALLEL_GNUSORT
template <typename size_type, typename lno_t, typename ExecutionSpace>
void kk_sort_by_row_size_parallel(
    const lno_t nv,
    const size_type *in_xadj,
    lno_t *new_indices, int sort_decreasing_order = 1, int num_threads=1){

  typedef Kokkos::RangePolicy<ExecutionSpace> my_exec_space;

  struct SortItem{ 
    lno_t id;
    lno_t size;
   bool operator<(const SortItem & a) const
  {
    return this->size > a.size;
  }
  };

  std::vector<SortItem> vnum_elements(nv);
  SortItem * num_elements = &(vnum_elements[0]);


  Kokkos::parallel_for( "KokkosKernels::Common::SortByRowSize::S0", my_exec_space(0, nv),
      KOKKOS_LAMBDA(const lno_t& row) {
        lno_t row_size = in_xadj[row+1] - in_xadj[row];
        num_elements[row].size = row_size; 
        num_elements[row].id = row;
      });
  __gnu_parallel::sort
  (&(num_elements[0]), &(num_elements[0])+nv,
      std::less<struct SortItem >());

      if (sort_decreasing_order == 1){
        Kokkos::parallel_for( "KokkosKernels::Common::SortByRowSize::S1", my_exec_space(0, nv),
        KOKKOS_LAMBDA(const lno_t& row) {
          new_indices[num_elements[row].id] = row;
        });
      }
      else if (sort_decreasing_order == 0){
        Kokkos::parallel_for( "KokkosKernels::Common::SortByRowSize::S2", my_exec_space(0, nv),
        KOKKOS_LAMBDA(const lno_t& row) {
          new_indices[num_elements[row].id] = nv - row - 1;
        });
      } 
      else {
        Kokkos::parallel_for( "KokkosKernels::Common::SortByRowSize::S3", my_exec_space(0, nv),
        KOKKOS_LAMBDA(const lno_t& row) {
          if (row   & 1){
          new_indices[num_elements[row].id] = nv - (row + 1) / 2;
          } 
          else {
          new_indices[num_elements[row].id] = row / 2 ;
          }
        });
      }
}
#endif

#ifdef KOKKOSKERNELS_HAVE_PARALLEL_GNUSORT
template <typename size_type, typename lno_t, typename ExecutionSpace>
void kk_sort_by_row_size(
    const lno_t nv,
    const size_type *in_xadj,
    lno_t *new_indices, int sort_decreasing_order = 1, int num_threads=64){

  std::cout << "Parallel Sort" << std::endl;
  kk_sort_by_row_size_parallel<size_type, lno_t, ExecutionSpace>(nv, in_xadj, new_indices, sort_decreasing_order, num_threads); 
}
#else
template <typename size_type, typename lno_t, typename ExecutionSpace>
void kk_sort_by_row_size(
    const lno_t nv,
    const size_type *in_xadj,
    lno_t *new_indices, int sort_decreasing_order = 1, int /*num_threads*/=64){

  std::cout << "Sequential Sort" << std::endl;
  kk_sort_by_row_size_sequential(nv, in_xadj, new_indices, sort_decreasing_order);
}
#endif

template <typename size_type, typename lno_t, typename ExecutionSpace, typename scalar_t = double>
void kk_get_lower_triangle_fill_parallel(
    const lno_t nv,
    const size_type ne,
    const size_type *in_xadj,
    const lno_t *in_adj,
    const scalar_t *in_vals,
    size_type *out_xadj,
    lno_t *out_adj,
    scalar_t *out_vals,
    const lno_t *new_indices = NULL,
    bool use_dynamic_scheduling = false,
    bool chunksize = 4,
    bool is_lower = true
    ){


  const int vector_size = kk_get_suggested_vector_size(nv, ne, kk_get_exec_space_type<ExecutionSpace>());
  const int suggested_team_size = kk_get_suggested_team_size(vector_size, kk_get_exec_space_type<ExecutionSpace>());
  const int team_work_chunk_size = suggested_team_size * chunksize;

  typedef LowerTriangularMatrix<size_type, lno_t, ExecutionSpace,scalar_t> ltm_t;
  ltm_t ltm (
      nv,
      in_xadj,
      in_adj,in_vals,
      new_indices,
      out_xadj,
      out_adj,out_vals,
      team_work_chunk_size,
      is_lower);


  typedef typename ltm_t::team_fill_policy_t fill_p_t;
  typedef typename ltm_t::dynamic_team_fill_policy_t d_fill_p_t;


  if (use_dynamic_scheduling){
    Kokkos::parallel_for( "KokkosKernels::Common::GetLowerTriangleFill::DynamicSchedule", d_fill_p_t(nv  / team_work_chunk_size + 1 , suggested_team_size, vector_size), ltm);
  }
  else {
    Kokkos::parallel_for( "KokkosKernels::Common::GetLowerTriangleFill::StaticSchedule", fill_p_t(nv  / team_work_chunk_size + 1 , suggested_team_size, vector_size), ltm);
  }
  ExecutionSpace().fence();
}
template <typename size_type, typename lno_t, typename scalar_t>
void kk_get_lower_triangle_fill_sequential(
    lno_t nv,
    const size_type *in_xadj,
    const lno_t *in_adj,
    const scalar_t *in_vals,
    const size_type *out_xadj,
    lno_t *out_adj,
    scalar_t *out_vals,
    const lno_t *new_indices = NULL
    ){
  for (lno_t i = 0; i < nv; ++i){
    lno_t row_index = i;

    if (new_indices) row_index = new_indices[i];
    size_type write_index = out_xadj[i];
    size_type begin = in_xadj[i];
    lno_t rowsize = in_xadj[i + 1] - begin;
    for (lno_t j = 0; j < rowsize; ++j){
      lno_t col = in_adj[j + begin];
      lno_t col_index = col;
      if (new_indices) col_index = new_indices[col];

      if (row_index > col_index){
        if (in_vals != NULL && out_vals != NULL){
          out_vals[write_index] = in_vals[j + begin];
        }
        out_adj[write_index++] = col;
      }
    }
  }
}
template <typename size_type, typename lno_t, typename ExecutionSpace>
void kk_get_lower_triangle_count(
    const lno_t nv, const size_type ne,
    const size_type *in_xadj,
    const lno_t *in_adj,
    size_type *out_xadj,
    const lno_t *new_indices = NULL,
    bool use_dynamic_scheduling = false,
    bool chunksize = 4,
    bool is_lower = true
    ){
  //Kokkos::Timer timer1;


  //kk_get_lower_triangle_count_sequential(nv, in_xadj, in_adj, out_xadj, new_indices);
  kk_get_lower_triangle_count_parallel<size_type, lno_t, ExecutionSpace>(
      nv, ne, in_xadj, in_adj, out_xadj, new_indices,use_dynamic_scheduling,chunksize, is_lower);
  //double count = timer1.seconds();
  //std::cout << "lower count time:" << count<< std::endl;

}
template <typename size_type, typename lno_t, typename scalar_t, typename ExecutionSpace>
void kk_get_lower_triangle_fill(
    lno_t nv, size_type ne,
    const size_type *in_xadj,
    const lno_t *in_adj,
    const scalar_t *in_vals,
    size_type *out_xadj,
    lno_t *out_adj,
    scalar_t *out_vals,
    const lno_t *new_indices = NULL,
    bool use_dynamic_scheduling = false,
    bool chunksize = 4,
    bool is_lower = true
    ){
  //Kokkos::Timer timer1;
/*
  kk_get_lower_triangle_fill_sequential(
    nv, in_xadj, in_adj,
    in_vals,
    out_xadj,
    out_adj,
    out_vals,
    new_indices
    );
*/


  kk_get_lower_triangle_fill_parallel<size_type, lno_t, ExecutionSpace, scalar_t>(
      nv,
      ne,
      in_xadj,
      in_adj,
      in_vals,
      out_xadj,
      out_adj,
      out_vals,
      new_indices,
      use_dynamic_scheduling,
      chunksize,
      is_lower
      );

  //double fill = timer1.seconds();
  //std::cout << "lower fill time:" << fill<< std::endl;

}



template <typename crstmat_t>
crstmat_t kk_get_lower_triangle(crstmat_t in_crs_matrix,
    typename crstmat_t::index_type::value_type *new_indices = NULL,
    bool use_dynamic_scheduling = false,
    bool chunksize = 4){

  typedef typename crstmat_t::execution_space exec_space;
  typedef typename crstmat_t::StaticCrsGraphType graph_t;
  typedef typename crstmat_t::row_map_type::non_const_type row_map_view_t;
  typedef typename crstmat_t::index_type::non_const_type   cols_view_t;
  typedef typename crstmat_t::values_type::non_const_type values_view_t;
  //typedef typename crstmat_t::row_map_type::const_type const_row_map_view_t;
  //typedef typename crstmat_t::index_type::const_type   const_cols_view_t;
  //typedef typename crstmat_t::values_type::const_type const_values_view_t;

  typedef typename row_map_view_t::non_const_value_type size_type;
  typedef typename cols_view_t::non_const_value_type lno_t;
  typedef typename values_view_t::non_const_value_type scalar_t;


  lno_t nr = in_crs_matrix.numRows();

  const scalar_t *vals = in_crs_matrix.values.data();
  const size_type *rowmap = in_crs_matrix.graph.row_map.data();
  const lno_t *entries= in_crs_matrix.graph.entries.data();
  const size_type ne = in_crs_matrix.graph.entries.extent(0);


  row_map_view_t new_row_map (Kokkos::view_alloc(Kokkos::WithoutInitializing, "LL"), nr + 1);
  kk_get_lower_triangle_count
  <size_type, lno_t, exec_space> (nr, ne, rowmap, entries, new_row_map.data(), new_indices, use_dynamic_scheduling, chunksize);

  kk_exclusive_parallel_prefix_sum
  <row_map_view_t, exec_space>(nr + 1, new_row_map);
  exec_space().fence();

  auto ll_size = Kokkos::subview(new_row_map, nr);
  auto h_ll_size = Kokkos::create_mirror_view (ll_size);
  Kokkos::deep_copy (h_ll_size, ll_size);
  size_type ll_nnz_size = h_ll_size();

  //cols_view_t new_entries ("LL", ll_nnz_size);
  //values_view_t new_values ("LL", ll_nnz_size);
  cols_view_t new_entries (Kokkos::view_alloc(Kokkos::WithoutInitializing, "LL"), ll_nnz_size);
  values_view_t new_values (Kokkos::view_alloc(Kokkos::WithoutInitializing, "LL"), ll_nnz_size);

  kk_get_lower_triangle_fill
  <size_type, lno_t, scalar_t, exec_space> (
      nr, ne, rowmap, entries, vals, new_row_map.data(),
      new_entries.data(), new_values.data(),new_indices, use_dynamic_scheduling, chunksize);

  graph_t g (new_entries, new_row_map);
  crstmat_t new_ll_mtx("lower triangle", in_crs_matrix.numCols(), new_values, g);
  return new_ll_mtx;
}

template <typename crstmat_t>
crstmat_t kk_get_lower_crs_matrix(crstmat_t in_crs_matrix,
    typename crstmat_t::index_type::value_type *new_indices = NULL,
    bool use_dynamic_scheduling = false,
    bool chunksize = 4){

  typedef typename crstmat_t::execution_space exec_space;
  typedef typename crstmat_t::StaticCrsGraphType graph_t;
  typedef typename crstmat_t::row_map_type::non_const_type row_map_view_t;
  typedef typename crstmat_t::index_type::non_const_type   cols_view_t;
  typedef typename crstmat_t::values_type::non_const_type values_view_t;
  //typedef typename crstmat_t::row_map_type::const_type const_row_map_view_t;
  //typedef typename crstmat_t::index_type::const_type   const_cols_view_t;
  //typedef typename crstmat_t::values_type::const_type const_values_view_t;

  typedef typename row_map_view_t::non_const_value_type size_type;
  typedef typename cols_view_t::non_const_value_type lno_t;
  typedef typename values_view_t::non_const_value_type scalar_t;


  lno_t nr = in_crs_matrix.numRows();

  const scalar_t *vals = in_crs_matrix.values.data();
  const size_type *rowmap = in_crs_matrix.graph.row_map.data();
  const lno_t *entries= in_crs_matrix.graph.entries.data();
  const size_type ne = in_crs_matrix.graph.entries.extent(0);


  row_map_view_t new_row_map (Kokkos::view_alloc(Kokkos::WithoutInitializing, "LL"), nr + 1);
  kk_get_lower_triangle_count
  <size_type, lno_t, exec_space> (nr, ne, rowmap, entries, new_row_map.data(), new_indices, use_dynamic_scheduling, chunksize);


  kk_exclusive_parallel_prefix_sum
  <row_map_view_t, exec_space>(nr + 1, new_row_map);
  exec_space().fence();

  auto ll_size = Kokkos::subview(new_row_map, nr);
  auto h_ll_size = Kokkos::create_mirror_view (ll_size);
  Kokkos::deep_copy (h_ll_size, ll_size);
  size_type ll_nnz_size = h_ll_size();

  //cols_view_t new_entries ("LL", ll_nnz_size);
  //values_view_t new_values ("LL", ll_nnz_size);
  cols_view_t new_entries (Kokkos::view_alloc(Kokkos::WithoutInitializing, "LL"), ll_nnz_size);
  values_view_t new_values (Kokkos::view_alloc(Kokkos::WithoutInitializing, "LL"), ll_nnz_size);

  kk_get_lower_triangle_fill
  <size_type, lno_t, scalar_t, exec_space> (
      nr, ne, rowmap, entries, vals, new_row_map.data(),
      new_entries.data(), new_values.data(),new_indices, use_dynamic_scheduling, chunksize);

  graph_t g (new_entries, new_row_map);
  crstmat_t new_ll_mtx("lower triangle", in_crs_matrix.numCols(), new_values, g);
  return new_ll_mtx;
}

template <typename graph_t>
graph_t kk_get_lower_crs_graph(graph_t in_crs_matrix,
    typename graph_t::data_type *new_indices = NULL,
    bool /*use_dynamic_scheduling*/ = false,
    bool /*chunksize*/ = 4){

  typedef typename graph_t::execution_space exec_space;

  typedef typename graph_t::row_map_type::non_const_type row_map_view_t;
  typedef typename graph_t::entries_type::non_const_type   cols_view_t;

  //typedef typename graph_t::row_map_type::const_type const_row_map_view_t;
  //typedef typename graph_t::entries_type::const_type   const_cols_view_t;


  typedef typename row_map_view_t::non_const_value_type size_type;
  typedef typename cols_view_t::non_const_value_type lno_t;



  lno_t nr = in_crs_matrix.numRows();
  const size_type *rowmap = in_crs_matrix.row_map.data();
  const lno_t *entries= in_crs_matrix.entries.data();

  const size_type ne = in_crs_matrix.graph.entries.extent(0);

  row_map_view_t new_row_map (Kokkos::view_alloc(Kokkos::WithoutInitializing, "LL"), nr + 1);
  kk_get_lower_triangle_count
  <size_type, lno_t, exec_space> (nr, ne, rowmap, entries, new_row_map.data(), new_indices);

  kk_exclusive_parallel_prefix_sum
  <row_map_view_t, exec_space>(nr + 1, new_row_map);
  exec_space().fence();

  auto ll_size = Kokkos::subview(new_row_map, nr);
  auto h_ll_size = Kokkos::create_mirror_view (ll_size);
  Kokkos::deep_copy (h_ll_size, ll_size);
  size_type ll_nnz_size = h_ll_size();

  cols_view_t new_entries (Kokkos::view_alloc(Kokkos::WithoutInitializing, "LL"), ll_nnz_size);


  kk_get_lower_triangle_fill
  <size_type, lno_t, lno_t, exec_space> (
      nr, ne, rowmap, entries, NULL, new_row_map.data(),
      new_entries.data(), NULL,new_indices);

  graph_t g (new_entries, new_row_map);

  return g;
}


template <typename row_map_view_t,
          typename cols_view_t,
          typename values_view_t,
          typename out_row_map_view_t,
          typename out_cols_view_t,
          typename out_values_view_t,
          typename new_indices_t,
          typename exec_space
          >
void kk_get_lower_triangle(
    typename cols_view_t::non_const_value_type nr,
    row_map_view_t in_rowmap,
    cols_view_t in_entries,
    values_view_t in_values,
    out_row_map_view_t &out_rowmap,
    out_cols_view_t &out_entries,
    out_values_view_t &out_values,
    new_indices_t &new_indices,
    bool use_dynamic_scheduling = false,
    bool chunksize = 4,
    bool is_lower = true){

  //typedef typename row_map_view_t::const_type const_row_map_view_t;
  //typedef typename cols_view_t::const_type   const_cols_view_t;
  //typedef typename values_view_t::const_type const_values_view_t;

  typedef typename row_map_view_t::non_const_value_type size_type;
  typedef typename cols_view_t::non_const_value_type lno_t;
  typedef typename values_view_t::non_const_value_type scalar_t;



  const scalar_t *vals = in_values.data();
  const size_type *rowmap = in_rowmap.data();
  const lno_t *entries= in_entries.data();
  const size_type ne = in_entries.extent(0);

  out_rowmap = out_row_map_view_t(Kokkos::view_alloc(Kokkos::WithoutInitializing, "LL"), nr + 1);
  kk_get_lower_triangle_count
  <size_type, lno_t, exec_space> (nr, ne, rowmap, entries,
      out_rowmap.data(),
      new_indices.data(),
      use_dynamic_scheduling,
      chunksize,  is_lower);

  kk_exclusive_parallel_prefix_sum
  <out_row_map_view_t, exec_space>(nr + 1, out_rowmap);
  exec_space().fence();

  auto ll_size = Kokkos::subview(out_rowmap, nr);
  auto h_ll_size = Kokkos::create_mirror_view (ll_size);
  Kokkos::deep_copy (h_ll_size, ll_size);
  size_type ll_nnz_size = h_ll_size();

  //cols_view_t new_entries ("LL", ll_nnz_size);
  //values_view_t new_values ("LL", ll_nnz_size);
  out_entries = out_cols_view_t(Kokkos::view_alloc(Kokkos::WithoutInitializing, "LL"), ll_nnz_size);

  if (in_values.data() != NULL)
    out_values = out_values_view_t (Kokkos::view_alloc(Kokkos::WithoutInitializing, "LL"), ll_nnz_size);

  kk_get_lower_triangle_fill
  <size_type, lno_t, scalar_t, exec_space> (
      nr, ne,
      rowmap, entries, vals,
      out_rowmap.data(), out_entries.data(), out_values.data(),
      new_indices.data(), use_dynamic_scheduling, chunksize,is_lower);
}

template <typename row_map_view_t,
          typename cols_view_t,
          typename out_row_map_view_t,
          typename out_cols_view_t,
          typename exec_space
          >
void kk_create_incidence_tranpose_matrix_from_lower_triangle(
    typename cols_view_t::non_const_value_type nr,
    row_map_view_t in_rowmap,
    cols_view_t in_entries,
    out_row_map_view_t &out_rowmap,
    out_cols_view_t &out_entries,
    bool /*use_dynamic_scheduling */ = false,
    bool /*chunksize*/ = 4){

  //typedef typename row_map_view_t::const_type const_row_map_view_t;
  //typedef typename cols_view_t::const_type   const_cols_view_t;

  typedef typename row_map_view_t::non_const_value_type size_type;
  typedef typename cols_view_t::non_const_value_type lno_t;

  //const size_type *rowmap = in_rowmap.data();
  //const lno_t *entries= in_entries.data();
  const size_type ne = in_entries.extent(0);
  out_rowmap = out_row_map_view_t(Kokkos::view_alloc(Kokkos::WithoutInitializing, "LL"), ne + 1);
  //const lno_t nr = in_rowmap.extent(0) - 1;
  typedef Kokkos::RangePolicy<exec_space> my_exec_space;

  Kokkos::parallel_for("KokkosKernels::Common::CreateIncidenceTransposeMatrixFromLowerTriangle::S0", my_exec_space(0, ne + 1),
      KOKKOS_LAMBDA(const lno_t& i) {
    out_rowmap[i] = i * 2;
    });


  //typedef Kokkos::TeamPolicy<exec_space> team_policy_t;
  //int vector_size = 2;
  //team_policy_t(ne)
  //nv  / team_work_chunk_size + 1 , suggested_team_size, vector_size

  out_entries = out_cols_view_t(Kokkos::view_alloc(Kokkos::WithoutInitializing, "LL"), 2 * ne);

  //TODO MAKE IT WITH TEAMS.
  Kokkos::parallel_for("KokkosKernels::Common::CreateIncidenceTransposeMatrixFromLowerTriangle::S1", my_exec_space(0, nr),
      KOKKOS_LAMBDA(const size_type& row) {
    size_type begin = in_rowmap(row);
    lno_t row_size = in_rowmap(row + 1) - begin;
    for (int i = 0; i < row_size; ++i){
      size_type edge_ind = i + begin;
      lno_t col = in_entries(edge_ind);
      edge_ind = edge_ind * 2;
      out_entries[edge_ind] = row;
      out_entries[edge_ind + 1] = col;
    }

    });
  }


template <typename row_map_view_t,
          typename cols_view_t,
          typename out_row_map_view_t,
          typename out_cols_view_t,
          typename permutation_view_t,
          typename exec_space
          >
void kk_create_incidence_matrix_from_original_matrix(
    typename cols_view_t::non_const_value_type nr,
    row_map_view_t in_rowmap,
    cols_view_t in_entries,
    out_row_map_view_t &out_rowmap,
    out_cols_view_t &out_entries,
    permutation_view_t permutation,
    bool use_dynamic_scheduling = false,
    bool chunksize = 4){

  //typedef typename row_map_view_t::const_type const_row_map_view_t;
  //typedef typename cols_view_t::const_type   const_cols_view_t;

  typedef typename row_map_view_t::non_const_value_type size_type;
  typedef typename cols_view_t::non_const_value_type lno_t;
  typedef Kokkos::RangePolicy<exec_space> my_exec_space;
  lno_t * perm = permutation.data();
  const size_type ne = in_entries.extent(0);

  out_rowmap = out_row_map_view_t (Kokkos::view_alloc(Kokkos::WithoutInitializing, "out_rowmap"), nr+1);
  out_entries = out_cols_view_t (Kokkos::view_alloc(Kokkos::WithoutInitializing, "out_cols_view"), ne);


  //todo: need to try both true and false
  bool sort_decreasing_order = true;
  //find the size of rows at upper triangular.
  //this gives the size of each column in lower triangluar.
  kk_get_lower_triangle_count
  <size_type, lno_t, exec_space> (nr, ne, in_rowmap.data(), in_entries.data(),
      out_rowmap.data(),
      permutation.data(),
      use_dynamic_scheduling,
      chunksize, sort_decreasing_order);
  exec_space().fence();
  kk_exclusive_parallel_prefix_sum<out_row_map_view_t, exec_space>(nr+1, out_rowmap);

  //kk_print_1Dview(out_rowmap, false, 20);

  out_row_map_view_t out_rowmap_copy (Kokkos::view_alloc(Kokkos::WithoutInitializing, "tmp"), nr+1);
  //out_rowmap = out_row_map_view_t("LL", nr+1);
  Kokkos::parallel_for("KokkosKernels::Common::CreateIncidenceTransposeMatrixFromOriginalTriangle::S0", my_exec_space(0, nr+1),
      KOKKOS_LAMBDA(const lno_t& i) {
    out_rowmap_copy[i] = in_rowmap[i];
  });

  if (sort_decreasing_order){
    Kokkos::parallel_for("KokkosKernels::Common::CreateIncidenceTransposeMatrixFromOriginalTriangle::S1", my_exec_space(0, nr),
        KOKKOS_LAMBDA(const size_type& row) {
      size_type begin = in_rowmap(row);
      lno_t row_size = in_rowmap(row + 1) - begin;

      lno_t row_perm = row;
      if (perm) row_perm = perm[row];
      //std::cout << "row:" << row << " rowperm:" << row_perm << std::endl;
      size_type used_edge_index = out_rowmap[row];
      lno_t used_count = 0;
      for (int i = 0; i < row_size; ++i){

        size_type edge_ind = i + begin;
        lno_t col = in_entries[edge_ind];

        lno_t col_perm = col;
        if (perm) col_perm = perm[col];
        if (row_perm > col_perm){
          typedef typename std::remove_reference< decltype( out_rowmap_copy[0] ) >::type atomic_incr_type;
          size_type row_write_index = Kokkos::atomic_fetch_add(&(out_rowmap_copy[row]), atomic_incr_type(1));
          size_type col_write_index = Kokkos::atomic_fetch_add(&(out_rowmap_copy[col]), atomic_incr_type(1));
          out_entries[row_write_index] = used_edge_index + used_count;
          out_entries[col_write_index] = used_edge_index + used_count;
          ++used_count;

        }
      }
    });


  }
  else {
    Kokkos::parallel_for("KokkosKernels::Common::CreateIncidenceTransposeMatrixFromOriginalTriangle::S2", my_exec_space(0, nr),
      KOKKOS_LAMBDA(const size_type& row) {
    size_type begin = in_rowmap(row);
    lno_t row_size = in_rowmap(row + 1) - begin;

    lno_t row_perm = row;
    if (perm) row_perm = perm[row];
    //std::cout << "row:" << row << " rowperm:" << row_perm << std::endl;
    size_type used_edge_index = out_rowmap[row];
    lno_t used_count = 0;
    for (int i = 0; i < row_size; ++i){

      size_type edge_ind = i + begin;
      lno_t col = in_entries[edge_ind];

      lno_t col_perm = col;
      if (perm) col_perm = perm[col];
      if (row_perm < col_perm){
        typedef typename std::remove_reference< decltype( out_rowmap_copy[0] ) >::type atomic_incr_type;
        size_type row_write_index = Kokkos::atomic_fetch_add(&(out_rowmap_copy[row]), atomic_incr_type(1));
        size_type col_write_index = Kokkos::atomic_fetch_add(&(out_rowmap_copy[col]), atomic_incr_type(1));
        out_entries[row_write_index] = used_edge_index + used_count;
        out_entries[col_write_index] = used_edge_index + used_count;
        ++used_count;

      }
    }
  });
  }


  //out_rowmap = out_row_map_view_t("LL", nr+1);
  Kokkos::parallel_for("KokkosKernels::Common::CreateIncidenceTransposeMatrixFromOriginalTriangle::S3", my_exec_space(0, nr+1),
      KOKKOS_LAMBDA(const lno_t& i) {
    out_rowmap[i] = in_rowmap[i];
  });
}



template<typename view_type>
struct ReduceLargerRowCount{

  view_type rowmap;
  typename view_type::const_value_type threshold;

  ReduceLargerRowCount(view_type view_to_reduce_, typename view_type::const_value_type threshold_): rowmap(view_to_reduce_), threshold(threshold_){}
  KOKKOS_INLINE_FUNCTION
  void operator()(const size_t &i, typename view_type::non_const_value_type &sum_reduction) const {
	  if (rowmap(i+1) - rowmap(i) > threshold){
		  sum_reduction += 1;
	  }
  }
};

template <typename view_type , typename MyExecSpace>
void kk_reduce_numrows_larger_than_threshold(size_t num_elements, view_type view_to_reduce,
		typename view_type::const_value_type threshold, typename view_type::non_const_value_type &sum_reduction){
  typedef Kokkos::RangePolicy<MyExecSpace> my_exec_space;
  Kokkos::parallel_reduce( "KokkosKernels::Common::ReduceNumRowsLargerThanThreshold", my_exec_space(0,num_elements), ReduceLargerRowCount<view_type>(view_to_reduce, threshold), sum_reduction);
}

}
}

#endif
