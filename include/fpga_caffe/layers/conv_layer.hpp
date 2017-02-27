#ifndef CONV_LAYER_HPP_
#define CONV_LAYER_HPP_

#define FADD_LATENCY 13

typedef struct {
  int inchannels;
  int outchannels;
  int burstchannels;
  int rpo;
  int rpofm;
  int burstydim;
  int ydim;
  int xdim; 
  int xtile_pad;
  int ksize;
  int numgroups;
  int numimages;
  int fc;
  int backward;
} kernel_params;

#endif
