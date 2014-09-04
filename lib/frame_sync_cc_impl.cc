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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gnuradio/io_signature.h>
#include "frame_sync_cc_impl.h"
#include <volk/volk.h>

#include <numeric> // for std::accumulate

namespace gr {
  namespace fbmc {

    frame_sync_cc::sptr
    frame_sync_cc::make(int L, int frame_len, std::vector<gr_complex> preamble_sym, int step_size, float threshold, int overlap)
    {
      return gnuradio::get_initial_sptr
        (new frame_sync_cc_impl(L, frame_len, preamble_sym, step_size, threshold, overlap));
    }

    /*
     * The private constructor
     */
    frame_sync_cc_impl::frame_sync_cc_impl(int L, int frame_len, std::vector<gr_complex> preamble_sym, int step_size, float threshold, int overlap)
      : gr::block("frame_sync_cc",
              gr::io_signature::make(1, 1, sizeof(gr_complex)),
              gr::io_signature::make(1, 1, sizeof(gr_complex))),
                d_L(L),
                d_frame_len(frame_len*L/2), // frame sync 'sees' half the rate compared to the rest of the FG
                d_fixed_lag_lookahead(3*L/2), // shift the correlation peak to the beginning of the frame
                d_preamble_sym(preamble_sym),
                d_step_size(step_size),
                d_threshold(threshold),
                d_overlap(overlap),
                d_state(FRAME_SYNC_PRESENCE_DETECTION),
                d_num_consec_frames(0),
                d_sample_ctr(0),
                d_pretracking_window(10*L),
                d_pretracking_ctr(0),
                d_cfo(0),
                d_phi(0)
    {
      dbg_fp = fopen("fs_lc.bin", "wb");
      dbg_fp2 = fopen("fs_in.bin", "wb");
      dbg_fp3 = fopen("fs_rc.bin", "wb");

      if(d_threshold <= 0 || d_threshold >= 1)
        throw std::runtime_error(std::string("Threshold must be between (0,1)")); 

      if(d_step_size > d_L)
        throw std::runtime_error(std::string("Step size must be smaller than or equal to the symbol length"));

      if(d_L < 32)
        std::cerr << "Low number of subcarriers. Increase to make frame synchronization more reliable." << std::endl;

      if(d_overlap != 4)
        throw std::runtime_error("Overlap must be four or else segmentation faults may occur");

      volk_32fc_x2_conjugate_dot_prod_32fc(&d_preamble_energy, &d_preamble_sym[0], &d_preamble_sym[0], d_preamble_sym.size());
      d_preamble_energy = abs(d_preamble_energy);

      d_pretracking_buf.clear();
      d_pretracking_buf.resize(std::max(d_L*2,int(d_preamble_sym.size())), 0);

      d_cfo_hist = boost::circular_buffer<float>(10); // 10 is just an arbitrary value...
      set_min_noutput_items(std::max(d_L,d_overlap/2*d_L + int(d_pretracking_buf.size()))); // that's what can be returned with each call to work
    }

    /*
     * Our virtual destructor.
     */
    frame_sync_cc_impl::~frame_sync_cc_impl()
    {
      fclose(dbg_fp);
      fclose(dbg_fp2);
      fclose(dbg_fp3);
    }

    void
    frame_sync_cc_impl::forecast (int noutput_items, gr_vector_int &ninput_items_required)
    {
        // this is a few samples more than we actually need
        ninput_items_required[0] = std::max(2*d_L + d_fixed_lag_lookahead + d_step_size, int(d_preamble_sym.size()) + d_step_size);
    }

    float 
    frame_sync_cc_impl::estimate_cfo(gr_complex corr_val)
    { 
      //std::cout << "corr:" << corr_val << ", f_off:" << 1.0/(2*M_PI*d_L)*arg(-corr_val) << std::endl;
      return -1.0/(2*M_PI*d_L)*arg(corr_val); 
    }

    float 
    frame_sync_cc_impl::avg_cfo(float cfo)
    {
      d_cfo_hist.push_front(cfo);
      float cfo_sum = 0;
      for(int i=0; i<d_cfo_hist.size(); i++)
        cfo_sum += d_cfo_hist[i];
      float cfo_avg = cfo_sum/d_cfo_hist.size();
      //std::cout << "cfo in: " << cfo << ", cb size: " << d_cfo_hist.size() << ", cfo avg: " << cfo_avg << std::endl;
      return cfo_avg;
    }

    gr_complex
    frame_sync_cc_impl::fixed_lag_corr(gr_complex *x)
    {
      // results for auto- and crosscorrelation
      gr_complex xcorr = 0;
      gr_complex acorr = 0;

      volk_32fc_x2_conjugate_dot_prod_32fc(&xcorr, x, x+d_L, d_L);
      volk_32fc_x2_conjugate_dot_prod_32fc(&acorr, x, x, d_L*2);

      // acorr is calculated over two symbols, so scale accordingly
      return gr_complex(2,0)*xcorr/acorr;
    }

    gr_complex
    frame_sync_cc_impl::ref_corr(gr_complex *x)
    {
      gr_complex corr = 0;
      gr_complex sig_energy = 0;
      volk_32fc_x2_conjugate_dot_prod_32fc(&corr, x, &d_preamble_sym[0], d_preamble_sym.size());
      volk_32fc_x2_conjugate_dot_prod_32fc(&sig_energy, x, x, d_preamble_sym.size());
      return corr/sqrt(d_preamble_energy*sig_energy);
    }

    std::string
    frame_sync_cc_impl::print_state()
    {
      if(d_state == FRAME_SYNC_PRESENCE_DETECTION)
        return "Presence detection";
      else if(d_state == FRAME_SYNC_ACQUISITION)
        return "Acquisition";
      else if(d_state == FRAME_SYNC_TRACKING)
        return "Tracking";
      else if(d_state == FRAME_SYNC_VALIDATION)
        return "Validation";
      else
        return "invalid state";
    }

    int
    frame_sync_cc_impl::general_work (int noutput_items,
                       gr_vector_int &ninput_items,
                       gr_vector_const_void_star &input_items,
                       gr_vector_void_star &output_items)
    {
      if( ninput_items[0] < 2*d_L)
        throw std::runtime_error(std::string("Not enough input items"));

      gr_complex *in = (gr_complex *) input_items[0];
      gr_complex *out = (gr_complex *) output_items[0];

      int samples_consumed = 0;
      int samples_returned = 0;

      //std::cout << "ctw, state: " << print_state() << std::endl;

      /* Acquisition algorithm:
      * Step 1: Perform correlation of two subsequent symbols about 2 symbols in advance. This is very robust against frequency offsets.
      *         The peak itself is too broad for a SOF detection but the phase around the maximum is very stable and can therefore be used for CFO correction
      *         This is needed as first step because the frequency offset degrades the correlation to the reference symbol severely.
      * Step 2: Perform correlation with reference symbol on the frequency corrected input signal (based on a NEW fixed lag correlation) to obtain the exact location of the peak and therefore the SOF.
                The fixed lag correlation has to be updated because DC offset can lead to a strong correlation and therefore false positives. 
                Using both criteria (strong fixed lag and reference correlation) ensures a low false alarm ratio and good detection probability.
      *         If this pre-tracking does not lead to a detection within the next L samples, go back to step 1
      *
      * Tracking algorithm:
      * Assume the synchronization to be valid for a whole frame. 
      * At the (expected) beginning of each frame perform a fixed lag correlation to update the frequency offset.
      * Then correlate with the reference symbol for the phase offset and confirm the validity of the expected SOF
      */

      // gr_complex res_flc = fixed_lag_corr(in+d_fixed_lag_lookahead);
      // d_pretracking_buf.assign(in, in+d_preamble_sym.size());
      // d_pretracking_buf.linearize();
      // gr_complex res_rc = ref_corr(&d_pretracking_buf[0]);
      // samples_consumed = 1;
      // fwrite(&res_flc, sizeof(gr_complex), 1, dbg_fp);
      // fwrite(&res_rc, sizeof(gr_complex), 1, dbg_fp3);
      // fwrite(in, sizeof(gr_complex), 1, dbg_fp2);

      if(d_state == FRAME_SYNC_PRESENCE_DETECTION)
      {
        gr_complex res = fixed_lag_corr(in+d_fixed_lag_lookahead);
        if(abs(res) > d_threshold)
        {
          std::cout << "DET->ACQ after successful fixed lag correlation" << std::endl;
          d_state = FRAME_SYNC_ACQUISITION;
          d_pretracking_ctr = 0;
          
          //float cfo = estimate_cfo(res); 
          //d_cfo_hist.clear(); // this resets the frequency offset averaging    
          //d_cfo = avg_cfo(cfo);
          //d_cfo = estimate_cfo();
          //std::cout << "-- cfo=" << d_cfo*500e3 << " Hz" << std::endl;         
          //for(int i=0; i < d_preamble_sym.size(); i++)
          //  in[i] *= exp(gr_complex(0,-2*M_PI*d_cfo*i));
          //d_phi = fmod(-2*M_PI*d_cfo*d_preamble_sym.size(), 2*M_PI);        
          //d_pretracking_buf.assign(in, in+d_preamble_sym.size());
          
          samples_consumed = 0;
          samples_returned = 0;
        }
        else
        {
          samples_consumed = d_step_size;
          samples_returned = 0;
        }
      }
      
      if(d_state == FRAME_SYNC_ACQUISITION)
      {
        gr_complex res_flc = fixed_lag_corr(in+d_fixed_lag_lookahead);
        if(abs(res_flc) > d_threshold)
        {
          d_cfo = estimate_cfo(res_flc);

          d_pretracking_buf.assign(in, in+d_pretracking_buf.size());
          for(int i=0; i < d_pretracking_buf.size(); i++)
            d_pretracking_buf[i] *= exp(gr_complex(0,-2*M_PI*d_cfo*i));

          if(!d_pretracking_buf.is_linearized())
            d_pretracking_buf.linearize();
          gr_complex res_rc = ref_corr(&d_pretracking_buf[0]);

          if(abs(res_rc) > d_threshold)
          {
            std::cout << "ACQ->TRACK after successful reference correlation" << std::endl;
            d_state = FRAME_SYNC_TRACKING;
            d_phi = arg(res_rc);
            std::cout << "cfo*250e3=" << d_cfo*250e3 << ", phi=" << d_phi << std::endl;

            d_cfo_hist.clear();
            d_cfo = avg_cfo(d_cfo);
            for(int i=0; i<d_pretracking_buf.size(); i++)
              d_pretracking_buf[i] *= exp(gr_complex(0,-d_phi));
            d_phi += fmod(2*M_PI*d_cfo*d_pretracking_buf.size(), 2*M_PI);

            memcpy(out, &d_pretracking_buf[0], sizeof(gr_complex)*d_pretracking_buf.size());
            d_sample_ctr = d_pretracking_buf.size();

            samples_consumed = d_pretracking_buf.size();
            samples_returned = d_pretracking_buf.size();
          }
          else
          {
            d_pretracking_ctr++;
            if(d_pretracking_ctr >= d_pretracking_window)
            {
              std::cout << "ACQ->DET after " << d_pretracking_window << " unsuccessful trials" << std::endl;
              d_state = FRAME_SYNC_PRESENCE_DETECTION;
            }      

            samples_consumed = 1;
            samples_returned = 0;      
          }
        }
        else
        {
          d_pretracking_ctr++;
          if(d_pretracking_ctr >= d_pretracking_window)
          {
            std::cout << "ACQ->DET after " << d_pretracking_window << " unsuccessful trials" << std::endl;
            d_state = FRAME_SYNC_PRESENCE_DETECTION;
          }

          samples_consumed = 1;
          samples_returned = 0;
        }
      }
      else if(d_state == FRAME_SYNC_TRACKING)
      {
        int samples_until_eof = d_frame_len - d_sample_ctr;
        int samples_to_return = std::min(samples_until_eof, ninput_items[0]);
        samples_to_return = std::min(samples_to_return, noutput_items);

        for(int i=0; i < samples_to_return; i++)
          in[i] *= exp(gr_complex(0,-2*M_PI*d_cfo*i - d_phi));
        d_phi = fmod(d_phi + 2*M_PI*d_cfo*samples_to_return, 2*M_PI);

        memcpy(out, in, sizeof(gr_complex)*samples_to_return);

        d_sample_ctr += samples_to_return;
        if(d_sample_ctr == d_frame_len)
        {
          d_state = FRAME_SYNC_VALIDATION;
          std::cout << "TRACK->VAL after a complete frame" << std::endl;          
        } 

        samples_consumed = samples_to_return;
        samples_returned = samples_to_return;
      }
      else if(d_state == FRAME_SYNC_VALIDATION)
      {
        d_sample_ctr = 0;
        gr_complex res = fixed_lag_corr(in+d_fixed_lag_lookahead);
        if( abs(res) > d_threshold )
        {
          //float cfo = estimate_cfo(res);
          //d_cfo = avg_cfo(cfo);
          d_cfo = avg_cfo(estimate_cfo(res));
          std::cout << "-- cfo*250e3=" << d_cfo*250e3 << std::endl;

          d_pretracking_buf.assign(in, in+d_pretracking_buf.size());
          for(int i=0; i < d_pretracking_buf.size(); i++)
            d_pretracking_buf[i] *= exp(gr_complex(0,-2*M_PI*d_cfo*i));
          d_phi = 2*M_PI*d_cfo*d_pretracking_buf.size();

          if(!d_pretracking_buf.is_linearized())
            d_pretracking_buf.linearize();
          
          res = ref_corr(&d_pretracking_buf[0]);
          if(abs(res) > d_threshold)
          {
            // std::cout << "VAL->TRACK after successful fixed lag and reference correlation" << std::endl;
            d_state = FRAME_SYNC_TRACKING;

            for(int i=0; i < d_pretracking_buf.size(); i++)
              d_pretracking_buf[i] *= exp(gr_complex(0,-arg(res)));
            d_phi = fmod(d_phi + arg(res), 2*M_PI);
            std::cout << "--phi=" << arg(res) << " rad" << std::endl;

            // repeat the beginning of the new frame to allow the sync to move the frame boundaries by a few samples without causing interference
            memcpy(out, &d_pretracking_buf[0], sizeof(gr_complex)*d_overlap/2*d_L);
            memcpy(out+d_overlap/2*d_L, &d_pretracking_buf[0], sizeof(gr_complex)*d_pretracking_buf.size());
            d_sample_ctr = d_pretracking_buf.size();

            samples_consumed = d_preamble_sym.size();
            samples_returned = d_preamble_sym.size()+d_L*d_overlap/2;
          }
          else
          {
            std::cout << "VAL->DET after unsuccessful reference correlation" << std::endl;
            d_state = FRAME_SYNC_PRESENCE_DETECTION;

            samples_consumed = 1;
            samples_returned = 0;
          }
        }
        else
        {
          std::cout << "VAL->DET after unsuccessful fixed lag correlation" << std::endl;
          d_state = FRAME_SYNC_PRESENCE_DETECTION;

          samples_consumed = 1;
          samples_returned = 0;
        }

      }

      // inform the scheduler about what has been going on...
      //std::cout << "consumed:" << samples_consumed << ", returned:" << samples_returned << std::endl;
      consume_each(samples_consumed);
      if(noutput_items < samples_returned)
        throw std::runtime_error(std::string("Output buffer too small"));

      return samples_returned;            
    }

  } /* namespace fbmc */
} /* namespace gr */

