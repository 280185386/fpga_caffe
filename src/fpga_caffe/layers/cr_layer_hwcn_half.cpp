#include <stdio.h>
#include <assert.h>
#include <string.h>
#include <stdbool.h>

#include "../../../include/fpga_caffe/layer.hpp"
#include "../../../include/fpga_caffe/half.hpp"
#include "../../../include/fpga_caffe/vector_types.hpp"

#define OCFACT 1 

/* Kernel used for computing direct convolution forward and backward. 
 * input:         flattened input array containing image data
 * weights:       convolution filters
 * bias:          flattened bias array
 * output:        output of the convolution
 */ 

chalf16 max9(chalf16 pool_inbuf[9][16 * 16], int n, short16 *out_mask) {
#pragma HLS INLINE
  chalf16 reduce_s1[4];
#pragma HLS ARRAY_PARTITION variable=reduce_s1 complete
  chalf16 reduce_s2[2];
#pragma HLS ARRAY_PARTITION variable=reduce_s2 complete
  chalf16 reduce_s3;
  chalf16 reduce_s4;

  short16 mask_s0[9];
#pragma HLS ARRAY_PARTITION variable=mask_s0 complete
  short16 mask_s1[4];
#pragma HLS ARRAY_PARTITION variable=mask_s1 complete
  short16 mask_s2[2];
#pragma HLS ARRAY_PARTITION variable=mask_s2 complete
  short16 mask_s3;

  short16 mask_s4;

  for (int i = 0; i < 9; ++i)
    mask_s0[i] = i;

  for (int i = 0; i < 4; ++i)
    reduce_s1[i] = max(pool_inbuf[i * 2][n], pool_inbuf[i * 2 + 1][n],
        mask_s0[i * 2], mask_s0[i * 2 + 1], &mask_s1[i]);

  for (int i = 0; i < 2; ++i)
    reduce_s2[i] = max(reduce_s1[i * 2], reduce_s1[i * 2 + 1],
        mask_s1[i * 2], mask_s1[i * 2 + 1], &mask_s2[i]);

  reduce_s3 = max(reduce_s2[0], reduce_s2[1], mask_s2[0], mask_s2[1],
      &mask_s3);

  reduce_s4 = max(reduce_s3, pool_inbuf[8][n], mask_s3, mask_s0[8],
      &mask_s4);

  *out_mask = mask_s4;

  return reduce_s4;
}

int mode_select_idx(int idx_fw, int idx_bw, bool mode) {
#pragma HLS INLINE
  if (mode)
    return idx_bw;
  else
    return idx_fw;
}

short mode_select_size(short size_fw, short size_bw, bool mode) {
#pragma HLS INLINE
  if (mode)
    return size_bw;
  else
    return size_fw;
}

chalf relu_bw(chalf input, bool enable) {
#pragma HLS INLINE off
  chalf res = (enable) ? input : chalf(0);
  return res;
}

void relu_fw(chalf16 outbuf[OCFACT][256], short outbuf_relu[OCFACT][256],
    int num_iter) {
  RELU_FW: for (int i = 0; i < num_iter; ++i) {
  #pragma HLS pipeline
    for (int k = 0; k < OCFACT; ++k) {
      chalf16 val = max(outbuf[k][i]);
      outbuf[k][i] = val;
      short relu_out = 0;
      relu_out |= (val.s0 != chalf(0)) ? 1 << 0 : 0;
      relu_out |= (val.s1 != chalf(0)) ? 1 << 1 : 0;
      relu_out |= (val.s2 != chalf(0)) ? 1 << 2 : 0;
      relu_out |= (val.s3 != chalf(0)) ? 1 << 3 : 0;
      relu_out |= (val.s4 != chalf(0)) ? 1 << 4 : 0;
      relu_out |= (val.s5 != chalf(0)) ? 1 << 5 : 0;
      relu_out |= (val.s6 != chalf(0)) ? 1 << 6 : 0;
      relu_out |= (val.s7 != chalf(0)) ? 1 << 7 : 0;
      relu_out |= (val.s8 != chalf(0)) ? 1 << 8 : 0;
      relu_out |= (val.s9 != chalf(0)) ? 1 << 9 : 0;
      relu_out |= (val.sa != chalf(0)) ? 1 << 10 : 0;
      relu_out |= (val.sb != chalf(0)) ? 1 << 11 : 0;
      relu_out |= (val.sc != chalf(0)) ? 1 << 12 : 0;
      relu_out |= (val.sd != chalf(0)) ? 1 << 13 : 0;
      relu_out |= (val.se != chalf(0)) ? 1 << 14 : 0;
      relu_out |= (val.sf != chalf(0)) ? 1 << 15 : 0;
      outbuf_relu[k][i] = relu_out;
    }
  }
}

extern "C" {

void cr_layer_hwcn_half(chalf16 *input, chalf16 *weights, chalf *bias,
    chalf16 *output, short *track_relu, int *params, int group_idx) { 
// Ports 
#pragma HLS data_pack variable=weights
#pragma HLS data_pack variable=output
#pragma HLS data_pack variable=input
#pragma HLS INTERFACE m_axi port=input offset=slave bundle=gmem1
#pragma HLS INTERFACE m_axi port=output offset=slave bundle=gmem2
#pragma HLS INTERFACE m_axi port=weights offset=slave bundle=gmem3
#pragma HLS INTERFACE m_axi port=bias offset=slave bundle=gmem4
#pragma HLS INTERFACE m_axi port=track_relu offset=slave bundle=gmem5
#pragma HLS INTERFACE m_axi port=params offset=slave bundle=gmem6
#pragma HLS INTERFACE s_axilite port=input bundle=control
#pragma HLS INTERFACE s_axilite port=output bundle=control
#pragma HLS INTERFACE s_axilite port=weights bundle=control
#pragma HLS INTERFACE s_axilite port=bias bundle=control
#pragma HLS INTERFACE s_axilite port=track_relu bundle=control
#pragma HLS INTERFACE s_axilite port=params bundle=control
#pragma HLS INTERFACE s_axilite port=group_idx bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control

  // Input tile buffer
  chalf16 inbuf[4][2 * 256 * 16];
#pragma HLS ARRAY_PARTITION variable=inbuf complete dim=1

  short inbuf_relu[4][2 * 256 * 16];
#pragma HLS ARRAY_PARTITION variable=inbuf_relu complete dim=1

  short outbuf_relu[OCFACT][256];
#pragma HLS ARRAY_PARTITION variable=outbuf_relu complete dim=1

  // Output buffer used for writing
  chalf16 outbuf[OCFACT][256];
#pragma HLS ARRAY_PARTITION variable=outbuf complete dim=1

  // Weight buffer
  chalf16 wbuf[OCFACT][256];
#pragma HLS ARRAY_PARTITION variable=wbuf complete dim=1

  // Bias buffer
  chalf biasbuf[4096];
DO_PRAGMA(HLS ARRAY_PARTITION variable=biasbuf cyclic factor=OCFACT)

  chalf16 pool_inbuf[9][16 * 16];
#pragma HLS ARRAY_PARTITION variable=pool_inbuf complete dim=1

  chalf16 pool_outbuf[16 * 16];

  chalf16 pool_outbuf_b[9][16 * 16];
#pragma HLS ARRAY_PARTITION variable=pool_outbuf_b complete dim=1

  chalf16 pool_inbuf_b[16 * 16];

  short out_mask[16 * 256];
#pragma HLS ARRAY_PARTITION variable=out_mask cyclic factor=16 dim=1

  short in_mask[16 * 256];
#pragma HLS ARRAY_PARTITION variable=in_mask cyclic factor=16 dim=1

  chalf multres[OCFACT][4][16];
#pragma HLS ARRAY_PARTITION variable=multres complete dim=1
#pragma HLS ARRAY_PARTITION variable=multres complete dim=2
#pragma HLS ARRAY_PARTITION variable=multres complete dim=3

  chalf weight_fw[16];
#pragma HLS ARRAY_PARTITION variable=weight_fw complete

  chalf weight_val[4][16];
#pragma HLS ARRAY_PARTITION variable=weight_val complete dim=1
#pragma HLS ARRAY_PARTITION variable=weight_val complete dim=2

  chalf in_val[4][16];
#pragma HLS ARRAY_PARTITION variable=in_val complete dim=1
#pragma HLS ARRAY_PARTITION variable=in_val complete dim=2

  chalf addres_s1[OCFACT][32];
#pragma HLS ARRAY_PARTITION variable=addres_s1 complete dim=1
#pragma HLS ARRAY_PARTITION variable=addres_s1 complete dim=2

  chalf addres_s2[OCFACT][16];
#pragma HLS ARRAY_PARTITION variable=addres_s2 complete dim=1
#pragma HLS ARRAY_PARTITION variable=addres_s2 complete dim=2

  chalf addres_s3[OCFACT][4][2];
#pragma HLS ARRAY_PARTITION variable=addres_s3 complete dim=1
#pragma HLS ARRAY_PARTITION variable=addres_s3 complete dim=2
#pragma HLS ARRAY_PARTITION variable=addres_s3 complete dim=3

  chalf finalOut[OCFACT][16];
#pragma HLS ARRAY_PARTITION variable=finalOut complete dim=1
#pragma HLS ARRAY_PARTITION variable=finalOut complete dim=2

  chalf addres_s4[OCFACT][4];
#pragma HLS ARRAY_PARTITION variable=addres_s4 complete dim=1
#pragma HLS ARRAY_PARTITION variable=addres_s4 complete dim=2

  chalf addres_f[OCFACT][16];
#pragma HLS ARRAY_PARTITION variable=addres_f complete dim=1
#pragma HLS ARRAY_PARTITION variable=addres_f complete dim=2

  chalf wUpdate[OCFACT][16];
#pragma HLS ARRAY_PARTITION variable=wUpdate complete dim=1
#pragma HLS ARRAY_PARTITION variable=wUpdate complete dim=2

  chalf acc[OCFACT][16];
#pragma HLS ARRAY_PARTITION variable=acc complete dim=1
#pragma HLS ARRAY_PARTITION variable=acc complete dim=2

  short rfw[16];
#pragma HLS ARRAY_PARTITION variable=rfw complete dim=1

  bool relu_en[OCFACT][16];
#pragma HLS ARRAY_PARTITION variable=relu_en complete dim=1
#pragma HLS ARRAY_PARTITION variable=relu_en complete dim=2

  short inchannels = params[0];
  short outchannels = params[1];
  short burstchannels = params[2];
  short rpo = params[3];
  ap_uint<10> ydim = params[6];
  ap_uint<10> xdim = params[7];
  ap_uint<5> ksize = params[9];
  short numgroups = params[10];
  ap_uint<10> numimages = params[11];
  short fc = params[12];
  short relu = params[13];
  short backward = params[14];
  ap_uint<4> stride = params[15];
  ap_uint<4> pad = params[16];
  bool mode = (backward == 1);
  short pool = params[17];
  ap_uint<3> pksize = params[18];

  assert((pksize == 2) || (pksize == 3));
  assert(ksize <= 11);
  assert(ksize >= 1);
  assert(burstchannels <= 2048);
  assert(burstchannels >= 4);
  assert(numimages >= 192);
  assert(numimages <= 256);

  ap_uint<10> xdim_out = ((xdim - ksize + 2 * pad) / stride) + 1;
  ap_uint<10> ydim_out = xdim_out;

  ap_uint<8> img_fact = numimages >> 4;
  short burst_fact = burstchannels >> 2;
  short ic_fact = (inchannels % 16 == 0) ? (inchannels >> 4) :
    (inchannels >> 4) + 1;
  short wc_fact = (burstchannels % 16 == 0) ? (burstchannels >> 4) :
    (burstchannels >> 4) + 1;
  int bias_offset = outchannels * group_idx;
  memcpy(biasbuf, bias + bias_offset, sizeof(chalf) * outchannels);
  short out_div = outchannels / OCFACT;
  short ofm_iters = (outchannels % OCFACT == 0) ? out_div : out_div + 1;
 
  if (pool == 0) { 
    for (int n = 0; n < rpo; ++n) {
      for (int y = 0; y < ydim_out; ++y) {
        for (int x = 0; x < xdim_out; ++x) {
          ap_uint<8> yk_off = 0;
          ap_uint<8> xk_off = 0;
          ap_uint<8> yksize = 0;
          ap_uint<8> xksize = 0;
          bool xkset = false;
          bool ykset = false;
          for (int p = 0; p < ksize; ++p) {
            for (int q = 0; q < ksize; ++q) {
              short in_y = y * stride - pad + p;
              short in_x = x * stride - pad + q;
              int in_idx = (((in_y * xdim + in_x) * numgroups + group_idx) *
                  inchannels + n * burstchannels) * img_fact;
              int inbuf_idx = (p * ksize + q) * burst_fact * img_fact;
              short in_size = burst_fact * img_fact;

              if (in_y >= 0 && in_y < ydim) {
                if (q == 0)
                  yksize++;
                if (!ykset) {
                  yk_off = p;
                  ykset = true;
                }
              }
              if (in_x >= 0 && in_x < xdim) {
                if (p == 0)
                  xksize++;
                if (!xkset) {
                  xk_off = q;
                  xkset = true;
                }
              }

              if (in_y >= 0 && in_y < ydim && in_x >= 0 && in_x < xdim) {
                if ((x != 0) && (stride == 1) && (q != ksize - 1)) {
                  short q_off = burst_fact * img_fact;
                  SHIFT_LOOP: for (int i = 0; i < in_size; ++i) {
#pragma HLS pipeline
#pragma HLS dependence variable=inbuf inter false
#pragma HLS dependence variable=inbuf_relu inter false
                    for (int j = 0; j < 4; ++j) {
                      inbuf[j][i + inbuf_idx] = inbuf[j][i + inbuf_idx
                        + q_off];
                      if ((backward != 0) && relu)
                        inbuf_relu[j][i + inbuf_idx] =
                          inbuf_relu[j][i + inbuf_idx + q_off];
                    }
                  }
                } else {
                  for (int j = 0; j < 4; ++j) {
                    int f_in_idx = in_idx + j * burst_fact * img_fact;
                    memcpy(inbuf[j] + inbuf_idx, input + f_in_idx,
                        sizeof(chalf16) * in_size);
                    if ((backward != 0) && relu)
                      memcpy(inbuf_relu[j] + inbuf_idx, track_relu + f_in_idx,
                          sizeof(short) * in_size);
                  }
                }
              }
            }
          }

          for (int o = 0; o < ofm_iters; ++o) {
            if (n == 0 && (backward == 0)) {
              for (int i = 0; i < img_fact; ++i) {
#pragma HLS pipeline
                for (int k = 0; k < OCFACT; ++k) {
                  outbuf[k][i] = biasbuf[o * OCFACT + k];
                }
              }
            } else {
              for (int k = 0; k < OCFACT; ++k) {
                int out_idx, out_idx_f, out_idx_b;
                short out_size, out_size_f, out_size_b;
                out_idx_b = (o * OCFACT + k + outchannels * group_idx) * ksize
                  * ksize * ic_fact + n * ksize * ksize * wc_fact;
                out_size_b = ksize * ksize * wc_fact;
                out_idx_f = (((y * xdim_out + x) * numgroups + group_idx) *
                  outchannels + (o * OCFACT) + k) * img_fact;
                out_size_f = img_fact;

                out_idx = mode_select_idx(out_idx_f, out_idx_b, mode);
                out_size = mode_select_size(out_size_f, out_size_b, mode);

                if (o * OCFACT + k < outchannels)
                  memcpy(outbuf[k], output + out_idx, sizeof(chalf16) *
                      out_size);
              }
            }
            
            for (int k = 0; k < OCFACT; ++k) {
              int w_idx_f, w_idx_b, w_idx;
              short w_size_f, w_size_b, w_size;
              w_idx_b = (((y * xdim_out + x) * numgroups + group_idx) *
                  outchannels + (o * OCFACT + k)) * img_fact;
              w_size_b = img_fact;
              w_idx_f = (o * OCFACT + k + outchannels * group_idx) * ksize *
                ksize * ic_fact + n * ksize * ksize * wc_fact;
              w_size_f = ksize * ksize * wc_fact;

              w_idx = mode_select_idx(w_idx_f, w_idx_b, mode);
              w_size = mode_select_size(w_size_f, w_size_b, mode);

              if (o * OCFACT + k < outchannels)
                memcpy(wbuf[k], weights + w_idx, sizeof(chalf16) * w_size);
            }

            ap_uint<8> w_off = 0;
            ap_uint<5> img_off = 0;
            ap_uint<8> iter = 0;
            ap_uint<8> xdim_off = 0;
            ap_uint<8> ydim_off = 0;
            ap_uint<2> counter_bw = 0;
            ap_uint<2> counter_fw = 0;
            short mac_iterations = yksize * xksize * img_fact * burst_fact;
            MAC_LOOP: for (int i = 0; i < mac_iterations; ++i, ++iter,
              ++counter_bw) {
#pragma HLS pipeline
#pragma HLS DEPENDENCE variable outbuf inter false
#pragma HLS DEPENDENCE variable outbuf_relu inter false
#pragma HLS DEPENDENCE variable finalOut inter false
#pragma HLS DEPENDENCE variable wUpdate inter false
              if (!mode) {
                if (iter == img_fact) {
                  if (w_off == burst_fact - 1) {
                    counter_fw = 0;
                    w_off = 0;
                    if (xdim_off == xksize - 1) {
                      xdim_off = 0;
                      ydim_off++;
                    } else {
                      xdim_off++;
                    }
                  } else {
                    counter_fw++;
                    w_off++;
                  }
                  iter = 0;
                }
                img_off = iter;
              } else {
                if (iter == burst_fact) {
                  if (xdim_off == xksize - 1) {
                    xdim_off = 0;
                    if (ydim_off == yksize - 1) {
                      ydim_off = 0;
                      img_off++;
                    } else {
                      ydim_off++;
                    }
                  } else {
                    xdim_off++;
                  }
                  iter = 0;
                }
                w_off = iter;
              }

              short filt_off = (yk_off + ydim_off) * ksize + xk_off + xdim_off;
              short w_idx_f = filt_off * wc_fact + (w_off >> 2);
              short w_idx_b = img_off;
              short w_idx = (mode) ? w_idx_b : w_idx_f;
              short fout_idx = counter_bw * 4;
              short in_idx = (filt_off * burst_fact + w_off) * img_fact
                + img_off;
              short out_idx_f = img_off;
              short out_idx_b = filt_off * wc_fact + (w_off >> 2);
              short out_idx = (mode) ? out_idx_b : out_idx_f;
              bool acc_enable = (mode) ? (counter_bw == 3) : true;
              bool relu_on = (relu && ((backward == 1) ||
                  (backward == 2) || (backward == 3)));

              for (int k = 0; k < OCFACT; ++k) {
                weight_fw[0] = wbuf[k][w_idx].s0;
                weight_fw[1] = wbuf[k][w_idx].s1;
                weight_fw[2] = wbuf[k][w_idx].s2;
                weight_fw[3] = wbuf[k][w_idx].s3;   
                weight_fw[4] = wbuf[k][w_idx].s4;
                weight_fw[5] = wbuf[k][w_idx].s5;
                weight_fw[6] = wbuf[k][w_idx].s6;
                weight_fw[7] = wbuf[k][w_idx].s7;
                weight_fw[8] = wbuf[k][w_idx].s8;
                weight_fw[9] = wbuf[k][w_idx].s9;
                weight_fw[10] = wbuf[k][w_idx].sa;
                weight_fw[11] = wbuf[k][w_idx].sb;
                weight_fw[12] = wbuf[k][w_idx].sc;
                weight_fw[13] = wbuf[k][w_idx].sd;
                weight_fw[14] = wbuf[k][w_idx].se;
                weight_fw[15] = wbuf[k][w_idx].sf;
                for (int m = 0; m < 4; ++m) {
                  for (int j = 0; j < 16; ++j) {
                    if (mode)
                      weight_val[m][j] = weight_fw[j];
                    else
                      weight_val[m][j] = weight_fw[counter_fw * 4 + m];
                  }

                  short relu_val = inbuf_relu[m][in_idx];
                  bool fw_mode = (backward == 0);

                  for (int j = 0; j < 16; ++j)
                    relu_en[k][j] = (relu_on && ((relu_val >> j) & 0x1)) ||
                      fw_mode || (relu == 0);

                  in_val[m][0] = relu_bw(inbuf[m][in_idx].s0, relu_en[k][0]);
                  in_val[m][1] = relu_bw(inbuf[m][in_idx].s1, relu_en[k][1]);
                  in_val[m][2] = relu_bw(inbuf[m][in_idx].s2, relu_en[k][2]);
                  in_val[m][3] = relu_bw(inbuf[m][in_idx].s3, relu_en[k][3]);
                  in_val[m][4] = relu_bw(inbuf[m][in_idx].s4, relu_en[k][4]);
                  in_val[m][5] = relu_bw(inbuf[m][in_idx].s5, relu_en[k][5]);
                  in_val[m][6] = relu_bw(inbuf[m][in_idx].s6, relu_en[k][6]);
                  in_val[m][7] = relu_bw(inbuf[m][in_idx].s7, relu_en[k][7]);
                  in_val[m][8] = relu_bw(inbuf[m][in_idx].s8, relu_en[k][8]);
                  in_val[m][9] = relu_bw(inbuf[m][in_idx].s9, relu_en[k][9]);
                  in_val[m][10] = relu_bw(inbuf[m][in_idx].sa, relu_en[k][10]);
                  in_val[m][11] = relu_bw(inbuf[m][in_idx].sb, relu_en[k][11]);
                  in_val[m][12] = relu_bw(inbuf[m][in_idx].sc, relu_en[k][12]);
                  in_val[m][13] = relu_bw(inbuf[m][in_idx].sd, relu_en[k][13]);
                  in_val[m][14] = relu_bw(inbuf[m][in_idx].se, relu_en[k][14]);
                  in_val[m][15] = relu_bw(inbuf[m][in_idx].sf, relu_en[k][15]);

                  for (int j = 0; j < 16; ++j) 
                    multres[k][m][j] = in_val[m][j] * weight_val[m][j];
                }

                for (int off = 0; off < 2; ++off) {
                  for (int m = 0; m < 2; ++m) {
                    for (int j = 0; j < 8; ++j) {
                      chalf temp1, temp2;
                      if (mode) {
                        temp1 = multres[k][off * 2 + m][j * 2];
                        temp2 = multres[k][off * 2 + m][j * 2 + 1];
                      } else {
                        temp1 = multres[k][off * 2 + 0][m * 8 + j];
                        temp2 = multres[k][off * 2 + 1][m * 8 + j];
                      }
                      addres_s1[k][(off * 2 + m) * 8 + j] = temp1 + temp2;
                    }
                  }
                }
                for (int off = 0; off < 2; ++off) {
                  for (int m = 0; m < 2; ++m) {
                    for (int j = 0; j < 4; ++j) {
                      chalf temp1, temp2;
                      if (mode) {
                        temp1 = addres_s1[k][(off * 2 + m) * 8 + j * 2];
                        temp2 = addres_s1[k][(off * 2 + m) * 8 + j * 2 + 1];
                      } else {
                        temp1 = addres_s1[k][(off * 2 + m) * 4 + j];
                        temp2 = addres_s1[k][(off * 2 + m) * 4 + j + 16];
                      }
                      addres_s2[k][(off * 2 + m) * 4 + j] = temp1 + temp2;
                    }
                  }
                }

                for (int m = 0; m < 4; ++m) {
                  for (int j = 0; j < 2; ++j)
                    addres_s3[k][m][j] = addres_s2[k][m * 4 + j * 2] +
                      addres_s2[k][m * 4 + j * 2 + 1];
                  addres_s4[k][m] = addres_s3[k][m][0] + addres_s3[k][m][1];
                }

                for (int m = 0; m < 4; ++m)
                  wUpdate[k][fout_idx + m] = addres_s4[k][m];

                for (int j = 0; j < 16; ++j) {
                  if (mode)
                    finalOut[k][j] = wUpdate[k][j];
                  else
                    finalOut[k][j] = addres_s2[k][j];
                }
                if (acc_enable) {
                  outbuf[k][out_idx] += finalOut[k];
                }               
              }
            }
            if (relu && (backward == 0) && (n == rpo - 1)) {
              relu_fw(outbuf, outbuf_relu, img_fact);
            }

            for (int k = 0; k < OCFACT; ++k) {
              int out_idx, out_idx_f, out_idx_b;
              short out_size, out_size_f, out_size_b;
              out_idx_b = (o * OCFACT + k + outchannels * group_idx) * ksize *
                ksize * ic_fact + n * ksize * ksize * wc_fact;
              out_size_b = ksize * ksize * wc_fact;
              out_idx_f = (((y * xdim_out + x) * numgroups + group_idx) *
                  outchannels + (o * OCFACT) + k) * img_fact;
              out_size_f = img_fact;

              out_idx = mode_select_idx(out_idx_f, out_idx_b, mode);
              out_size = mode_select_size(out_size_f, out_size_b, mode);

              if (relu && (o * OCFACT + k < outchannels) && (backward == 0) &&
                  (n == rpo - 1)) {
                memcpy(track_relu + out_idx, outbuf_relu[k], sizeof(short) *
                    out_size);
              }

              if (o * OCFACT + k < outchannels)
                memcpy(output + out_idx, outbuf[k], sizeof(chalf16) *
                    out_size);
            }
          }
        }
      }
    }

  } else {
    short pooled_height = ydim - pksize;
    if ((pooled_height & 0x1) == 1)
      pooled_height = (pooled_height >> 1) + 2;
    else
      pooled_height = (pooled_height >> 1) + 1;

    short pooled_width = pooled_height;

    if (backward == 0) {
      for (int ph = 0; ph < pooled_height; ++ph) {
        for (int pw = 0; pw < pooled_width; ++pw) {
          int hstart = ph * 2;
          int wstart = pw * 2;
          for (int c = 0; c < rpo; ++c) {
            for (int h = 0; h < 3; ++h) {
              for (int w = 0; w < 3; ++w) {
                int in_idx = (((hstart + h) * xdim + (wstart + w)) *
                    inchannels + c * burstchannels) * img_fact;
                if ((hstart + h < ydim) && (wstart + w < xdim) &&
                    (h < pksize) && (w < pksize))
                  memcpy(pool_inbuf[h * 3 + w], input + in_idx,
                      sizeof(chalf16) * img_fact * burstchannels);
                else
                  for (int n = 0; n < (img_fact * burstchannels) >> 1; ++n)
#pragma HLS pipeline
                    for (int j = 0; j < 2; ++j)
                      pool_inbuf[h * 3 + w][n * 2 + j] = chalf(CHALF_MIN_VAL);
              }
            }
            POOL_LOOP: for (int n = 0; n < (img_fact * burstchannels) >> 1;
              ++n) {
#pragma HLS pipeline
              for (int j = 0; j < 2; ++j) {
                short16 mask;
                pool_outbuf[n * 2 + j] = max9(pool_inbuf, n * 2 + j, &mask);
                out_mask[(n * 2 + j) * 16 + 0] = mask.s0;
                out_mask[(n * 2 + j) * 16 + 1] = mask.s1;
                out_mask[(n * 2 + j) * 16 + 2] = mask.s2;
                out_mask[(n * 2 + j) * 16 + 3] = mask.s3;
                out_mask[(n * 2 + j) * 16 + 4] = mask.s4;
                out_mask[(n * 2 + j) * 16 + 5] = mask.s5;
                out_mask[(n * 2 + j) * 16 + 6] = mask.s6;
                out_mask[(n * 2 + j) * 16 + 7] = mask.s7;
                out_mask[(n * 2 + j) * 16 + 8] = mask.s8;
                out_mask[(n * 2 + j) * 16 + 9] = mask.s9;
                out_mask[(n * 2 + j) * 16 + 10] = mask.sa;
                out_mask[(n * 2 + j) * 16 + 11] = mask.sb;
                out_mask[(n * 2 + j) * 16 + 12] = mask.sc;
                out_mask[(n * 2 + j) * 16 + 13] = mask.sd;
                out_mask[(n * 2 + j) * 16 + 14] = mask.se;
                out_mask[(n * 2 + j) * 16 + 15] = mask.sf;
              }
            }
            int out_idx = ((ph * pooled_width + pw) * inchannels +
                c * burstchannels) * img_fact;
            memcpy(output + out_idx, pool_outbuf, sizeof(chalf16) *
                img_fact * burstchannels);
            memcpy(track_relu + out_idx * 16, out_mask,
                sizeof(short) * numimages * burstchannels);
          }
        }
      }
    } else {
      for (int ph = 0; ph < pooled_height; ++ph) {
        for (int pw = 0; pw < pooled_width; ++pw) {
          for (int c = 0; c < rpo; ++c) {
            int hstart = ph * 2;
            int wstart = pw * 2;
            int in_idx = ((ph * pooled_width + pw) * inchannels + c *
                burstchannels) * img_fact;
            memcpy(pool_inbuf_b, input + in_idx, sizeof(chalf16) * img_fact
                * burstchannels);
            memcpy(in_mask, track_relu + in_idx * 16,
                sizeof(short) * numimages * burstchannels);
            for (int h = 0; h < 3; ++h) {
              for (int w = 0; w < 3; ++w) {
                int out_idx = (((hstart + h) * xdim + (wstart + w))
                    * inchannels + c * burstchannels) * img_fact;
                if ((hstart + h < ydim) && (wstart + w < xdim) &&
                    (h < pksize) && (w < pksize) && ((h == 0) || (w == 0)) &&
                    (pksize == 3)) {
                  memcpy(pool_outbuf_b[h * 3 + w], output + out_idx,
                      sizeof(chalf16) * img_fact * burstchannels);
                } else {
                  for (int n = 0; n < img_fact * burstchannels; ++n) {
#pragma HLS pipeline
                    pool_outbuf_b[h * 3 + w][n] = chalf(0);
                  }
                }
              }
            }
            for (int n = 0; n < img_fact * burstchannels; ++n) {
#pragma HLS pipeline
#pragma HLS DEPENDENCE variable pool_inbuf inter false
              pool_outbuf_b[in_mask[n * 16 + 0]][n].s0 += pool_inbuf_b[n].s0;
              pool_outbuf_b[in_mask[n * 16 + 1]][n].s1 += pool_inbuf_b[n].s1;
              pool_outbuf_b[in_mask[n * 16 + 2]][n].s2 += pool_inbuf_b[n].s2;
              pool_outbuf_b[in_mask[n * 16 + 3]][n].s3 += pool_inbuf_b[n].s3;
              pool_outbuf_b[in_mask[n * 16 + 4]][n].s4 += pool_inbuf_b[n].s4;
              pool_outbuf_b[in_mask[n * 16 + 5]][n].s5 += pool_inbuf_b[n].s5;
              pool_outbuf_b[in_mask[n * 16 + 6]][n].s6 += pool_inbuf_b[n].s6;
              pool_outbuf_b[in_mask[n * 16 + 7]][n].s7 += pool_inbuf_b[n].s7;
              pool_outbuf_b[in_mask[n * 16 + 8]][n].s8 += pool_inbuf_b[n].s8;
              pool_outbuf_b[in_mask[n * 16 + 9]][n].s9 += pool_inbuf_b[n].s9;
              pool_outbuf_b[in_mask[n * 16 + 10]][n].sa += pool_inbuf_b[n].sa;
              pool_outbuf_b[in_mask[n * 16 + 11]][n].sb += pool_inbuf_b[n].sb;
              pool_outbuf_b[in_mask[n * 16 + 12]][n].sc += pool_inbuf_b[n].sc;
              pool_outbuf_b[in_mask[n * 16 + 13]][n].sd += pool_inbuf_b[n].sd;
              pool_outbuf_b[in_mask[n * 16 + 14]][n].se += pool_inbuf_b[n].se;
              pool_outbuf_b[in_mask[n * 16 + 15]][n].sf += pool_inbuf_b[n].sf;
            }
            for (int h = 0; h < 3; ++h) {
              for (int w = 0; w < 3; ++w) {
                int out_idx = (((hstart + h) * xdim + (wstart + w))
                    * inchannels + c * burstchannels) * img_fact;
                if ((hstart + h < ydim) && (wstart + w < xdim) &&
                    (h < pksize) && (w < pksize))
                  memcpy(output + out_idx, pool_outbuf_b[h * 3 + w],
                      sizeof(chalf16) * img_fact * burstchannels);

              }
            } 
          }
        }
      }
    }
  }
}

}
