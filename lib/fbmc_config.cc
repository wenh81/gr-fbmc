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

#include "fbmc_config.h"
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <boost/format.hpp>
#include <numeric>
#include <iostream>
#include <fftw3.h>
#include <boost/circular_buffer.hpp>
#include <gnuradio/digital/constellation.h>


namespace gr {
  namespace fbmc {
    fbmc_config::fbmc_config(std::vector<int> channel_map, int num_payload_bits,
                             int num_overlap_sym, int samp_rate) :
        d_channel_map(channel_map), d_num_payload_bits(
            num_payload_bits), d_num_overlap_sym(num_overlap_sym), d_samp_rate(samp_rate)
    {
      // user parameter validity check
      check_user_args();

      // calculate all the variables defined by the c'tor parameters
      d_num_total_subcarriers = d_channel_map.size();
      d_num_used_subcarriers = 0;
      for(int i=0; i<d_num_total_subcarriers; i++)
      {
          if(d_channel_map[i] != 0)
          {
              d_num_used_subcarriers++;
          }
      }

//      if(d_num_used_subcarriers >= d_num_total_subcarriers) {
//          std::cerr << "WARNING: Invalid number of used subcarriers" << std::endl;
//          d_num_used_subcarriers = d_num_total_subcarriers - 1;
//      }

      d_num_payload_sym = std::ceil(float(d_num_payload_bits) / d_num_used_subcarriers); // PAM uses 1 bit per symbol

      // generate the prototype filter taps
      gen_prototype_filter();
      d_group_delay = (d_prototype_taps.size() - 1) / 2;

      // generate frequency domain preamble for insertion into the tx frame
      gen_preamble_sym();

      // generate time domain preamble for correlation in the receiver
      gen_ref_preamble_sym();
      d_num_preamble_sym = d_preamble_sym.size() / d_num_total_subcarriers; // another num_overlap_sym is needed to clear the filter registers
      d_num_sync_sym = d_num_overlap_sym
          + d_preamble_sym.size() / d_num_total_subcarriers; // num_overlap_sym is needed to settle the filters
      d_num_sym_frame = d_num_sync_sym + d_num_payload_sym + 2*d_num_overlap_sym; // total symbols per frame

      // print a short summary of the parameters to stdout
      print_info();
    }

    void
    fbmc_config::gen_prototype_filter()
    {

      /*
       * 		Calculates IOTA pulse function by orthogonalizing a gaussian pulse in the
       time and frequency domain as shown in [0] and [1].
       *
       *      [0] Cosine-modulated filterbanks based on extended Gaussian functions
       by Siohan, Pierre and Roche, Christian
       in IEEE Transactions on Signal Processing (Nov 2000)

       [1] Derivation of Extended Gaussian Functions Based on the Zak Transform
       by Siohan, Pierre and Roche, Christian
       in IEEE Signal Processing Letters (Mar 2004)
       */

      float tab[15][8] =
        {
          {1.0, 3.0 / 4, 105.0 / 64, 675.0 / 256, 76233.0 / 16384, 457107.0
              / 65536, 12097169.0 / 1048576, 70545315.0 / 4194304},
          {-1.0, -15.0 / 8, -219.0 / 64, -6055.0 / 1024, -161925.0 / 16384,
              -2067909.0 / 131072, -26060847.0 / 1048576, 0},
          {3.0 / 4, 16.0 / 19, 1545.0 / 512, 9765.0 / 2048, 596227.0 / 65536,
              3679941.0 / 262144, 3941559701.0 / 1677216, 0},
          {-5.0 / 8, -123.0 / 128, -2289.0 / 1024, -34871.0 / 8192, -969375.0
              / 131072, -51182445.0 / 4194304, 0, 0},
          {35.0 / 64, 213.0 / 256, 7797.0 / 4096, 56163.0 / 16384, 13861065.0
              / 2097152, 87185895.0 / 8388608, 0, 0},
          {-63.0 / 128, -763.0 / 1024, -13875.0 / 8192, -790815.0 / 262144,
              -23600537.0 / 4194304, 0, 0, 0},
          {231.0 / 512, 1395.0 / 2048, 202281.0 / 131072, 1434705.0 / 524288,
              85037895.0 / 16777216, 0, 0, 0},
          {-429.0 / 1024, -20691.0 / 32768, -374325.0 / 262144, -5297445.0
              / 2097152, 0, 0, 0, 0},
          {6435.0 / 16384, 38753.0 / 65536, 1400487.0 / 1048576, 9895893.0
              / 4194304, 0, 0, 0, 0},
          {-12155.0 / 32768, -146289.0 / 262144, -2641197.0 / 2097152, 0, 0, 0,
              0, 0},
          {46189.0 / 131072, 277797.0 / 524288, 20050485.0 / 16777216, 0, 0, 0,
              0, 0},
          {-88179.0 / 262144, -2120495.0 / 4194304, 0, 0, 0, 0, 0, 0},
          {676039.0 / 2097152, 4063017.0 / 8388608, 0, 0, 0, 0, 0, 0},
          {-1300075.0 / 4194304, 0, 0, 0, 0, 0, 0, 0},
          {5014575.0 / 16777216, 0, 0, 0, 0, 0, 0, 0}};

      // initialize table as std::valarray for convenient handling
      for(int i = 0; i < 15; i++){
        d_b[i] = std::valarray<float>(8);
        for(int n = 0; n < 8; n++)
          d_b[i][n] = tab[i][n];
      }

      int sps = d_num_total_subcarriers / 2;
      int delay = d_num_overlap_sym;

      // Distance between zeros in time (tau0) and frequency(v0)
      double tau0 = 1.0 / std::sqrt(2.0);
      double v0 = 1.0 / std::sqrt(2.0);
      if(std::abs(tau0 * v0 - 0.5) > 1e-6)
        throw std::runtime_error(
            "assertion std::abs(tau0*v0-0.5) <= 1e-6 failed");

      // Gaussian function parameter
      float alpha = 1.0;
      // Check alpha [1, above Eq. (15)]
      float alpha_m_v0 = 0.528 * std::pow(v0, 2);
      float alpha_m_tau0 = 1.892 / std::pow(tau0, 2);
      if(alpha_m_v0 > alpha)
        throw std::runtime_error("assertion alpha_m_v0 <= alpha failed");
      if(alpha > alpha_m_tau0)
        throw std::runtime_error("assertion alpha <= alpha_m_tau0 failed");

      // Build impulse response

      // Select sample times [0, Eq. (13)]
      std::valarray<float> t(2 * sps * delay + 1);
      for(int i = -sps * delay; i < sps * delay + 1; i++)
        t[i + sps * delay] = tau0 / sps * i;

      // Number of iterations for sums in Eq. (7) [0, below Eq. (27)]
      int K = 14;

      // Calculate IOTA impulse response [0, Eq. (7)] or [1, Eq. (15)] with c = 2
      std::valarray<float> s1(0.0, t.size());
      std::valarray<float> s2(0.0, t.size());

      for(int k = 0; k < K + 1; k++){
        // Elements of first sum
        s1 += d(k, alpha, v0, K)
            * (gauss(t + float(k) / float(v0), alpha)
                + gauss(t - float(k) / float(v0), alpha));
        // Elements of second sum
        s2 += d(k, 1.0 / alpha, tau0, K)
            * std::cos(float(2.0 * M_PI * k) * t / float(tau0));
      }

      // construct impulse response
      std::valarray<float> imp_res = float(0.5) * s1 * s2;
      float norm_fac = std::sqrt((imp_res * imp_res).sum());
      imp_res /= norm_fac;
      d_prototype_taps = std::vector<gr_complex>(imp_res.size());
      d_prototype_taps_float = std::vector<float>(imp_res.size());
      for(int i = 0; i < imp_res.size(); i++){
        d_prototype_taps[i] = imp_res[i];
        d_prototype_taps_float[i] = imp_res[i];
      }

    }

    std::valarray<float>
    fbmc_config::gauss(std::valarray<float> x, float alpha)
    {
      return std::sqrt(std::sqrt(2.0 * alpha))
          * std::exp(
              -M_PI * alpha * std::pow(x, std::valarray<float>(2, x.size())));
    }

    float
    fbmc_config::d(int k, float alpha, float v0, int K)
    {
      // d Coefficients for closed form IOTA calculation
      if(std::abs(v0 - 1.0 / std::sqrt(2.0)) > 1e-6)
        throw std::runtime_error(
            "assertion std::abs(v0 - 1.0/std::sqrt(2.0)) < 1e-6 failed");

      if(K != 14)
        throw std::runtime_error("assertion K == 14 failed");

      // Number of iterations [0, below Eq. (27)]
      int jk = std::floor(float(K - k) / 2);

      std::valarray<float> tmp(jk + 1);
      for(int i = 0; i < jk + 1; i++)
        tmp[i] = 2 * i + k;
      std::valarray<float> fac1 = d_b[k][std::slice(0, jk + 1, 1)];
      std::valarray<float> fac2 = std::exp(
          float(-M_PI * alpha / (2.0 * std::pow(v0, float(2.0)))) * tmp);
      std::valarray<float> res = fac1 * fac2;

      return res.sum();
    }

    bool
    fbmc_config::check_user_args()
    {
      if(d_num_payload_bits < 1)
        throw std::runtime_error(std::string("Need at least 1 payload bit"));
      else if(d_num_overlap_sym != 4)
        throw std::runtime_error(std::string("Overlap must be 4"));
      else if(d_samp_rate <= 0)
        throw std::runtime_error(std::string("Invalid sample rate"));

      return true;
    }

    void
    fbmc_config::print_info()
    {
      std::cout << "**********************************************************"
          << std::endl;
      std::cout << "******************* FBMC parameters *********************"
          << std::endl;
      std::cout << "**********************************************************"
          << std::endl;
      std::cout << "FFT length:\t" << d_num_total_subcarriers << std::endl;
      float subc_spac = float(d_samp_rate) / d_num_total_subcarriers / 1000;
      std::cout << "Subcarrier spacing (kHz):\t" << subc_spac << std::endl;
      std::cout << "Number of used subcarriers:\t" << d_num_used_subcarriers
          << std::endl;
      std::cout << "Number of symbols per frame:\t" << d_num_sym_frame
          << std::endl;
      std::cout << "\t-> payload symbols:\t" << d_num_payload_sym << std::endl;
      std::cout << "\t-> sync symbols:\t" << d_num_sync_sym << std::endl;
      std::cout << "\t-> overlap guard symbols:\t" << d_num_overlap_sym
          << std::endl;
      float tsym = float(d_num_total_subcarriers) / d_samp_rate / 2;
      std::cout << "Symbol duration (ms):\t" << tsym * 1000 << std::endl;
      std::cout << "Frame duration (ms):\t" << tsym * 1000 * d_num_sym_frame
          << std::endl;
      std::cout << "Filterbank group delay:\t"
          << (d_prototype_taps.size() - 1) / 2 << std::endl;
      std::cout << "Bits per frame:\t" << d_num_payload_bits << std::endl;
      float payl_bit_rate = float(d_num_payload_bits) / (tsym * d_num_sym_frame);
      std::cout << "Payload bit rate (kbps):\t" << payl_bit_rate / 1000
          << std::endl;
      std::cout << "Spectral efficiency (b/Hz):\t"
          << payl_bit_rate / (d_num_used_subcarriers * subc_spac * 1000)
          << std::endl;
      std::cout << "**********************************************************"
          << std::endl << std::endl;
    }

    void
    fbmc_config::gen_preamble_sym()
    {
      if(d_num_used_subcarriers > pow(2, 16) - 1)
        throw std::runtime_error("Max length 2**16-1 of PN sequence exceeded");
      uint16_t start_state = 0xACE1u; /* Any nonzero start start will work. */
      uint16_t  lfsr = start_state;
      unsigned bit;
      unsigned period = 0;
      float output;

      for(int i = 0; i < 2 * d_num_total_subcarriers; i++){
        /* taps: 16 14 13 11; feedback polynomial: x^16 + x^14 + x^13 + x^11 + 1 */
        bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1;
        lfsr = (lfsr >> 1) | (bit << 15);
        output = ((lfsr % 2) - 0.5) * 2; // map bits to -1/1
        d_prbs.push_back(output);
      }

      if(d_channel_map.size() != d_num_total_subcarriers)
        throw std::runtime_error("Channel map too small");

      d_preamble_sym = std::vector<gr_complex>(4 * d_num_total_subcarriers,
                                               gr_complex(0, 0));
      int n = 0;
      for(int i = 0; i < d_num_total_subcarriers; i++){
        if(d_channel_map[i] == 0)
          d_preamble_sym[i] = gr_complex(0, 0);
        else{
          d_preamble_sym[i] = gr_complex(
              1.0 / sqrt(2) * d_prbs[n],
              1.0 / sqrt(2) * d_prbs[n + d_num_total_subcarriers]);
          n++;
        }
        d_preamble_sym[i + 2 * d_num_total_subcarriers] = d_preamble_sym[i];
      }
    }

    std::vector<float>
    fbmc_config::phydyas_frequency_taps(int overlap)
    {
      std::vector<float> taps;
      for(int i = 0; i < 2 * overlap - 1; i++){
        int k = i - overlap + 1;
        float tap = std::pow(-1.0f, float(k)) * get_phydyas_frequency_tap(k, overlap);
        taps.push_back(tap);
      }
      return taps;
    }

    std::vector<float>
    fbmc_config::phydyas_impulse_taps(int L, int overlap)
    {
      int num_taps = L * overlap + 1;
      std::vector<float> taps(num_taps, 0.0f);
      for(int m = 0; m < num_taps; m++){
        if(m == 0){
          taps[m] = 0.0f;
        }
        else if(m <= L * overlap / 2){
          taps[m] = get_phydyas_frequency_tap(0, overlap);

          for(int k = 1; k < overlap; k++){
            float tap = get_phydyas_frequency_tap(k, overlap);
            taps[m] += 2.0f * std::pow(-1.0f, k) * tap * std::cos(2 * M_PI * float(k * m) / float(overlap * L));
          }
          taps[m] /= 2.0f;
        }
        else{
          taps[m] = taps[num_taps - m - 1];
        }
      }
      return taps;
    }

    float
    fbmc_config::get_phydyas_frequency_tap(int k, const int overlap)
    {
      const int orig_k = k;
      std::vector<std::vector<float> > optimal_filter_coeffs;
      optimal_filter_coeffs.push_back(std::vector<float>()); // K = 0 N/A
      optimal_filter_coeffs.push_back(std::vector<float>()); // K = 1 N/A
      optimal_filter_coeffs.push_back(std::vector<float>()); // K = 2 N/A
      optimal_filter_coeffs.push_back(std::vector<float>(1, 0.91143783f)); // K = 3
      optimal_filter_coeffs.push_back(std::vector<float>(1, 0.97195983f)); // K = 4
      optimal_filter_coeffs.push_back(std::vector<float>()); // K = 5 N/A
      optimal_filter_coeffs.push_back(std::vector<float>(2, 0.99722723f)); // K = 6
      optimal_filter_coeffs[6][1] = 0.94136732f;
      optimal_filter_coeffs.push_back(std::vector<float>()); // K = 7 N/A
      optimal_filter_coeffs.push_back(std::vector<float>(3, 0.99988389f)); // K = 8
      optimal_filter_coeffs[8][1] = 0.99315513f;
      optimal_filter_coeffs[8][2] = 0.92708081f;

      if(optimal_filter_coeffs[overlap].size() == 0){
        std::cout << "WARNING: Unsupported overlap size. Returning 0.0f\n";
        return 0.0f;
      }

      k = std::abs(k);
      float tap = 0.0f;

      if(k == 0){tap = 1.0f;}
      else if(k < overlap / 2){tap = optimal_filter_coeffs[overlap][k - 1];}
      else if(k == overlap / 2){tap = 1.0f / std::sqrt(2.0f);}
      else if(k < overlap){
        tap = std::sqrt(1.0f - std::pow(optimal_filter_coeffs[overlap][overlap - k - 1], 2.0f));
      }
      else{tap = 0.0f;}
      return tap;
    }

    void
    fbmc_config::gen_ref_preamble_sym()
    {

      // to convert the frequency domain preamble into time domain, an IFFT and a filter operation is needed
      int nsym = d_preamble_sym.size() / d_num_total_subcarriers;
      fftwf_complex* buffer = (fftwf_complex*) fftwf_malloc(
          sizeof(fftw_complex) * d_num_total_subcarriers);
      std::vector<gr_complex> preamble_sym_fft(
          d_preamble_sym.size() + d_num_total_subcarriers * d_num_overlap_sym,
          gr_complex(0, 0));

      // Setup and execute FFT
      fftwf_plan fft_plan = fftwf_plan_dft_1d(d_num_total_subcarriers, buffer,
                                              buffer, FFTW_BACKWARD,
                                              FFTW_ESTIMATE);
      for(int i = 0; i < nsym; i++){
          std::memcpy(buffer, &d_preamble_sym[0] + i * d_num_total_subcarriers,
               d_num_total_subcarriers * sizeof(gr_complex));
        fftwf_execute(fft_plan);
          std::memcpy(&preamble_sym_fft[0] + i * d_num_total_subcarriers, buffer,
               d_num_total_subcarriers * sizeof(gr_complex));
      }

      fftwf_destroy_plan(fft_plan);
      fftwf_free(buffer);

      // set up polyphase filterbank
      // pad the prototype to an integer multiple length of L
      std::vector<gr_complex> prototype_taps = d_prototype_taps;
      while(prototype_taps.size() % d_num_total_subcarriers != 0)
        prototype_taps.push_back(gr_complex(0, 0));

      // prepare the filter branches
      int num_branch_taps = prototype_taps.size() / d_num_total_subcarriers * 2; // calculate number of taps per branch filter
      gr_complex** branch_taps = new gr_complex*[d_num_total_subcarriers];
      boost::circular_buffer<gr_complex>* branch_states =
          new boost::circular_buffer<gr_complex>[d_num_total_subcarriers];

      for(int l = 0; l < d_num_total_subcarriers; l++){
        // write the upsampled prototype coefficients into the branch filter
        branch_taps[l] = new gr_complex[num_branch_taps];
        memset((void*) branch_taps[l], 0, num_branch_taps * sizeof(gr_complex));
        int offset = 0;
        if(l >= d_num_total_subcarriers / 2)
          offset = 1;
        for(int n = 0; n < num_branch_taps; n++)
          if((n + offset) % 2) // tap is zero due to oversampling
            branch_taps[l][n] = 0;
          else
            branch_taps[l][n] = prototype_taps[l
                + (n / 2) * d_num_total_subcarriers];

        // set size of state registers and initialize them with zeros
        branch_states[l].set_capacity(num_branch_taps); // this includes the new sample in each iteration
        for(int i = 0; i < num_branch_taps; i++)
          branch_states[l].push_front(gr_complex(0, 0));
      }
      // set up buffer for filtered output
      std::vector<gr_complex> filtered_preamble_sym(preamble_sym_fft.size(),
                                                    gr_complex(0, 0));

      // perform the filtering
      for(int i = 0; i < preamble_sym_fft.size() / d_num_total_subcarriers;
          i++){
        for(int l = 0; l < d_num_total_subcarriers; l++){
          branch_states[l].push_front(
              preamble_sym_fft[i * d_num_total_subcarriers + l]);
          for(int n = 0; n < num_branch_taps; n++){
            filtered_preamble_sym[i * d_num_total_subcarriers + l] +=
                branch_states[l][n] * branch_taps[l][n];
          }
        }
      }

      delete[] branch_taps;
      delete[] branch_states;

      // add the first and second half of one symbol, thus shortening the sequence by a factor of 2 (--> output commutator)
      d_ref_preamble_sym.clear();
      d_ref_preamble_sym.resize(filtered_preamble_sym.size() / 2,
                                gr_complex(0, 0));
      for(int i = 0; i < filtered_preamble_sym.size() / d_num_total_subcarriers;
          i++){
        for(int n = 0; n < d_num_total_subcarriers / 2; n++)
          d_ref_preamble_sym[n + i * d_num_total_subcarriers / 2] =
              filtered_preamble_sym[n + i * d_num_total_subcarriers]
                  + filtered_preamble_sym[n + i * d_num_total_subcarriers
                      + d_num_total_subcarriers / 2];
      }
    }
  }
}
