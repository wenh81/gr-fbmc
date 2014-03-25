/* -*- c++ -*- */
/* 
 * Copyright 2014 Communications Engineering Lab (CEL), Karlsruhe Institute of Technology (KIT).
 * 
 * This is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3, or (at your option)
 * any later version.
 * 
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this software; see the file COPYING.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street,
 * Boston, MA 02110-1301, USA.
 */

#ifndef INCLUDED_FBMC_POLYPHASE_FILTERBANK_VCVC_IMPL_H
#define INCLUDED_FBMC_POLYPHASE_FILTERBANK_VCVC_IMPL_H

#include <fbmc/polyphase_filterbank_vcvc.h>

namespace gr {
  namespace fbmc {

    class polyphase_filterbank_vcvc_impl : public polyphase_filterbank_vcvc
    {
     private:
      std::vector<gr_complex> d_prototype_taps; // taps of the prototype filter
      int d_L; // vector length (number of filter branches)
      int d_num_branch_taps; // number of taps per filter branch
      gr_complex** d_branch_taps; // filter branch taps. 2d array of taps, one row per filter (L x d_num_branch_taps)
      int d_group_delay; // group delay of the filter bank

     public:
      polyphase_filterbank_vcvc_impl(std::vector<gr_complex> taps, int L);
      ~polyphase_filterbank_vcvc_impl();

      // Where all the action really happens
      int work(int noutput_items,
	       gr_vector_const_void_star &input_items,
	       gr_vector_void_star &output_items);
    };

  } // namespace fbmc
} // namespace gr

#endif /* INCLUDED_FBMC_POLYPHASE_FILTERBANK_VCVC_IMPL_H */
