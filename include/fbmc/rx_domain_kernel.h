/* -*- c++ -*- */
/* 
 * Copyright 2015 <+YOU OR YOUR COMPANY+>.
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

#ifndef INCLUDED_FBMC_RX_DOMAIN_KERNEL_H
#define INCLUDED_FBMC_RX_DOMAIN_KERNEL_H

#include <fbmc/api.h>
#include <fbmc/smt_kernel.h>
#include <gnuradio/fft/fft.h>

namespace gr {
  namespace fbmc {

    /*!
     * \brief FBMC RX implemented with RX domain filtering.
     * takes in synchronized sample stream.
     * outputs received vectors.
     *
     */
    class FBMC_API rx_domain_kernel: public smt_kernel
    {
    public:
      rx_domain_kernel(const std::vector<float> &taps, int L);
      ~rx_domain_kernel();


      int generic_work(gr_complex* out, const gr_complex* in, int noutput_items);

      // using directives are only SWIG necessities
      using smt_kernel::L;
      using smt_kernel::overlap;
      using smt_kernel::taps;
      using smt_kernel::generic_work_python;
      int fft_size(){return d_fft->inbuf_length();};

    protected:
      int get_noutput_items_for_ninput(int inbuf_size){1 + (inbuf_size - fft_size()) / (L() / 2);};

    private:
      gr::fft::fft_complex* d_fft;
      gr::fft::fft_complex* setup_fft(int L, int overlap);

      gr_complex* d_equalized;
      void equalize(gr_complex* outbuf, const gr_complex* inbuf);

      float* d_taps_al;
      int d_num_al_taps;
      float* setup_taps_array(std::vector<float> taps);
      void apply_taps(gr_complex* outbuf, const gr_complex* inbuf, const int outbuf_len);

      bool containsNaN(const gr_complex* buf, const int vec_length);
    };

  } // namespace fbmc
} // namespace gr

#endif /* INCLUDED_FBMC_RX_DOMAIN_KERNEL_H */

