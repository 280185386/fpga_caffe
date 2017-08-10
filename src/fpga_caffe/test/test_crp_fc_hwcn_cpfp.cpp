#include <cfloat>
#include <vector>
#include <string>
#include "gtest/gtest.h"

#include "fpga_caffe/test/test_fpga_caffe_main.hpp"

using std::min;
using std::max;

void ref_fc_layer_hwcn(std::vector<float> input, std::vector<float> weights,
    std::vector<float> bias, std::vector<float>& output,
    kernel_params params) {
  int out_idx, in_idx, k_idx;

  int inchannels = params.inchannels;
  int outchannels = params.outchannels;
  int numimages = params.numimages;


  // Convolution
  for (int n = 0; n < numimages; n++) {
    for (int o = 0; o < outchannels; o++) {
      for (int k = 0; k < inchannels / 4; k++) {
        for (int m = 0; m < 4; ++m) {
          out_idx = o * numimages + n;
          in_idx = (m * (inchannels / 4) + k) * numimages + n;
          k_idx = o * inchannels + k * 4 + m;
          output[out_idx] += input[in_idx] * weights[k_idx];
        }
      }
    }
  }
  for (int n = 0; n < numimages; n++) {
    for (int o = 0; o < outchannels; ++o) {
      out_idx = o * numimages + n;
      output[out_idx] += bias[o];
    }
  }
}

void ref_backward_fc_layer_hwcn(std::vector<float> input,
    std::vector<float> weights, std::vector<float>& output,
    kernel_params params) {
  int out_idx, in_idx, k_idx;

  int inchannels = params.inchannels;
  int outchannels = params.outchannels;
  int numimages = params.numimages;

  for (int n = 0; n < numimages; n++) {
    for (int o = 0; o < outchannels; o++) {
      for (int k = 0; k < inchannels / 4; k++) {
        for (int m = 0; m < 4; ++m) {
          out_idx = o * numimages + n;
          in_idx = (m * (inchannels / 4) + k) * numimages + n;
          k_idx = o * inchannels + k * 4 + m;
          output[k_idx] += input[in_idx] * weights[out_idx];
        }
      }
    }
  }
}

template <typename TypeParam>
class CRPFCHWCNCPFPTest : public OCLDeviceTest<TypeParam> {
  typedef typename TypeParam::Dtype Dtype;

 protected:
  CRPFCHWCNCPFPTest()
    : ocl("crp_layer_hwcn_cpfp.xclbin", "crp_layer_hwcn_cpfp") 
  {}
  virtual void SetUp() {
    params.resize(1);
    params[0].numgroups = 1;
    params[0].inchannels = 64;
    params[0].outchannels = 10;
    params[0].burstchannels = 64;
    params[0].rpo = 1;
    params[0].rpofm = 1;
    params[0].burstydim = 10;
    params[0].ydim = 1;
    params[0].xdim = 1;
    params[0].xtile_pad = 0;
    params[0].numimages = 256;
    params[0].ksize = 1;
    params[0].backward = 0;
    params[0].relu = 1;
    params[0].stride = 1;
    params[0].fc = 1;
    params[0].pad = 0;
    params[0].pool = 0;
    params[0].pksize = 2;
  }

  virtual ~CRPFCHWCNCPFPTest() {}
  
  OCLUtil ocl;
  std::vector<Dtype> input;
  std::vector<Dtype> input_pad;
  std::vector<cpfp> input_pad_cpfp;
  std::vector<Dtype> weights;
  std::vector<Dtype> weights_pad;
  std::vector<cpfp> weights_pad_cpfp;
  std::vector<Dtype> bias;
  std::vector<cpfp> bias_cpfp;
  std::vector<Dtype> hw_results;
  std::vector<cpfp> hw_results_cpfp;
  std::vector<Dtype> sw_results;
  std::vector<kernel_params> params;
  std::vector<short> relu_vals;
  std::vector<short> sw_relu_vals;
  cl_mem ocl_input;
  cl_mem ocl_weights;
  cl_mem ocl_output;
  cl_mem ocl_bias;
  cl_mem ocl_relu_vals;
  cl_mem ocl_params;
};

TYPED_TEST_CASE(CRPFCHWCNCPFPTest, TestOCLDtypesAndDevices);

TYPED_TEST(CRPFCHWCNCPFPTest, TestFCReLU1x1F_CPFP) {
  this->ocl.Setup();
  std::vector<kernel_params> params = this->params;
  std::vector<cl_event> events;

  for (int i = 0; i < params.size(); ++i) {
    int ksize = params[i].ksize;
    // Set sizes
    int insize = params[i].numimages * params[i].inchannels * params[i].ydim *
      params[i].xdim * params[i].numgroups;
    int wsize = params[i].outchannels * params[i].numgroups *
      params[i].inchannels * ksize * ksize;
    int outsize = params[i].numimages * params[i].outchannels * params[i].ydim
      * params[i].xdim * params[i].numgroups;
    int bsize = params[i].outchannels * params[i].numgroups;
    int events_size = params[i].numgroups;
    int relusize = (outsize > insize) ? outsize : insize;
    // Resize vectors
    this->input.resize(insize, 0);
    this->input_pad_cpfp.resize(insize, cpfp(0));
    this->weights.resize(wsize, 0);
    this->weights_pad_cpfp.resize(wsize, cpfp(0));
    this->bias.resize(bsize, 0);
    this->bias_cpfp.resize(bsize, cpfp(0));
    this->sw_results.resize(outsize, 0);
    this->hw_results.resize(outsize, 0);
    this->hw_results_cpfp.resize(outsize, cpfp(0));
    this->relu_vals.resize(relusize / 16, -1);
    events.resize(events_size);
    // Populate vectors
    fillVectorCPFP(this->input, 0.0, 1.0);
    fillVectorCPFP(this->weights, -1.0, 1.0);
    fillVectorCPFP(this->bias, -1.0, 1.0);
   
    toCPFP(this->input, this->input_pad_cpfp);
    toCPFP(this->weights, this->weights_pad_cpfp);
    toCPFP(this->bias, this->bias_cpfp);

    // Create buffers
    this->ocl_input = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(cpfp) * insize, NULL, NULL);
    this->ocl_weights = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(cpfp) * wsize, NULL, NULL);
    this->ocl_output = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_WRITE,
        sizeof(cpfp) * outsize, NULL, NULL);
    this->ocl_bias = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(cpfp) * bsize, NULL, NULL);
    this->ocl_relu_vals = clCreateBuffer(this->ocl.oclContext,
        CL_MEM_READ_WRITE, sizeof(short) * relusize / 16, NULL, NULL);
    this->ocl_params = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(kernel_params), NULL, NULL);

    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_input, CL_TRUE,
        0, sizeof(cpfp) * insize, this->input_pad_cpfp.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_weights, CL_TRUE,
        0, sizeof(cpfp) * wsize, this->weights_pad_cpfp.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_bias, CL_TRUE, 0,
        sizeof(cpfp) * bsize, this->bias_cpfp.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_relu_vals,
        CL_TRUE, 0, sizeof(short) * relusize / 16, this->relu_vals.data(), 0,
        NULL, NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_params, CL_TRUE,
        0, sizeof(kernel_params), &params[i], 0, NULL, NULL);

    for (int g = 0; g < params[i].numgroups; ++g) {
      clSetKernelArg(this->ocl.oclKernel, 0, sizeof(cl_mem),
          &this->ocl_input);
      clSetKernelArg(this->ocl.oclKernel, 1, sizeof(cl_mem), 
          &this->ocl_weights);
      clSetKernelArg(this->ocl.oclKernel, 2, sizeof(cl_mem),
          &this->ocl_bias);
      clSetKernelArg(this->ocl.oclKernel, 3, sizeof(cl_mem),
          &this->ocl_output);
      clSetKernelArg(this->ocl.oclKernel, 4, sizeof(cl_mem),
          &this->ocl_relu_vals);
      clSetKernelArg(this->ocl.oclKernel, 5, sizeof(cl_mem), 
          &this->ocl_params);
      clSetKernelArg(this->ocl.oclKernel, 6, sizeof(cl_int), 
          &g);
      clEnqueueTask(this->ocl.oclCommandQueue, this->ocl.oclKernel, 0, NULL,
          &(events[g]));
    }

    clWaitForEvents(events_size, events.data());

    clEnqueueReadBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(cpfp) * outsize, this->hw_results_cpfp.data(), 0, NULL,
        NULL);
    clEnqueueReadBuffer(this->ocl.oclCommandQueue, this->ocl_relu_vals,
        CL_TRUE, 0, sizeof(short) * relusize / 16, this->relu_vals.data(), 0,
        NULL, NULL);

    toFloat(this->hw_results_cpfp, this->hw_results);

    ref_fc_layer_hwcn(this->input, this->weights, this->bias,
        this->sw_results, params[i]);
    ref_relu_layer(this->sw_results);
    int size = params[i].numimages * params[i].outchannels *
      params[i].numgroups * params[i].ydim * params[i].xdim;
    for (int j = 0; j < size; ++j) {
      std::cout<<this->sw_results[j]<<" "<<this->hw_results[j]<<" relu: "<<
        ((this->relu_vals[j / 16] >> (j % 16)) & 0x1)<<std::endl;
      EXPECT_TRUE(checkEQ(this->sw_results[j],
            this->hw_results[j], 1e-1, 1e-1));
    }
    clReleaseMemObject(this->ocl_input);
    clReleaseMemObject(this->ocl_weights);
    clReleaseMemObject(this->ocl_output);
    clReleaseMemObject(this->ocl_bias);
    clReleaseMemObject(this->ocl_relu_vals);
    clReleaseMemObject(this->ocl_params);
  }
}

TYPED_TEST(CRPFCHWCNCPFPTest, TestFCReLU1x1B_CPFP) {
  this->ocl.Setup();
  std::vector<kernel_params> params = this->params;
  std::vector<cl_event> events;

  for (int i = 0; i < params.size(); ++i) {
    // Set sizes
    int ksize = params[i].ksize;
    params[i].backward = 1;
    int insize = params[i].numimages * params[i].inchannels * params[i].ydim *
      params[i].xdim * params[i].numgroups;
    int wsize = params[i].numimages * params[i].outchannels * params[i].ydim *
      params[i].xdim * params[i].numgroups;
    int outsize = params[i].outchannels * params[i].numgroups *
      params[i].inchannels * ksize * ksize;
    int bsize = params[i].outchannels * params[i].numgroups;
    int events_size = params[i].numgroups;
    events.clear();
    // Resize vectors
    this->input.resize(insize, 0);
    this->input_pad_cpfp.resize(insize, cpfp(0));
    this->weights.resize(wsize, 0);
    this->weights_pad_cpfp.resize(wsize, cpfp(0));
    this->bias.resize(bsize, 0);
    this->bias_cpfp.resize(bsize, cpfp(0));
    this->sw_results.resize(outsize, 0);
    this->hw_results.resize(outsize, 0);
    this->hw_results_cpfp.resize(outsize, cpfp(0));
    this->relu_vals.resize(wsize / 16, -1);
    events.resize(events_size);
    // Populate vectors
    fillVectorCPFP(this->input, -1.0, 1.0);
    fillVectorCPFP(this->weights, 0.0, 1.0);
    fillVectorCPFP(this->bias, -1.0, 1.0);
  
    toCPFP(this->input, this->input_pad_cpfp);
    toCPFP(this->weights, this->weights_pad_cpfp);
    toCPFP(this->bias, this->bias_cpfp);

    // Create buffers
    this->ocl_input = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(cpfp) * insize, NULL, NULL);
    this->ocl_weights = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(cpfp) * wsize, NULL, NULL);
    this->ocl_output = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_WRITE,
        sizeof(cpfp) * outsize, NULL, NULL);
    this->ocl_bias = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(cpfp) * bsize, NULL, NULL);
    this->ocl_relu_vals = clCreateBuffer(this->ocl.oclContext,
        CL_MEM_READ_WRITE, sizeof(short) * wsize / 16, NULL, NULL);
    this->ocl_params = clCreateBuffer(this->ocl.oclContext, CL_MEM_READ_ONLY,
        sizeof(kernel_params), NULL, NULL);

    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_input, CL_TRUE,
        0, sizeof(cpfp) * insize, this->input_pad_cpfp.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_weights, CL_TRUE,
        0, sizeof(cpfp) * wsize, this->weights_pad_cpfp.data(), 0, NULL,
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_bias, CL_TRUE, 0,
        sizeof(cpfp) * bsize, this->bias_cpfp.data(), 0, NULL, 
        NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_relu_vals,
        CL_TRUE, 0, sizeof(short) * wsize / 16, this->relu_vals.data(), 0,
        NULL, NULL);
    clEnqueueWriteBuffer(this->ocl.oclCommandQueue, this->ocl_params, CL_TRUE,
        0, sizeof(kernel_params), &params[i], 0, NULL, NULL);

    for (int g = 0; g < params[i].numgroups; ++g) {
      clSetKernelArg(this->ocl.oclKernel, 0, sizeof(cl_mem),
          &this->ocl_input);
      clSetKernelArg(this->ocl.oclKernel, 1, sizeof(cl_mem), 
          &this->ocl_weights);
      clSetKernelArg(this->ocl.oclKernel, 2, sizeof(cl_mem),
          &this->ocl_bias);
      clSetKernelArg(this->ocl.oclKernel, 3, sizeof(cl_mem),
          &this->ocl_output);
      clSetKernelArg(this->ocl.oclKernel, 4, sizeof(cl_mem),
          &this->ocl_relu_vals);
      clSetKernelArg(this->ocl.oclKernel, 5, sizeof(cl_mem), 
          &this->ocl_params);
      clSetKernelArg(this->ocl.oclKernel, 6, sizeof(cl_int), 
          &g);
      clEnqueueTask(this->ocl.oclCommandQueue, this->ocl.oclKernel, 0, NULL,
          &(events[g]));
    }

    clWaitForEvents(events_size, events.data());

    clEnqueueReadBuffer(this->ocl.oclCommandQueue, this->ocl_output, CL_TRUE,
        0, sizeof(cpfp) * outsize, this->hw_results_cpfp.data(), 0, NULL,
        NULL);

    toFloat(this->hw_results_cpfp, this->hw_results);

    ref_backward_fc_layer_hwcn(this->input, this->weights,
        this->sw_results, params[i]);
    int size = params[i].outchannels * params[i].numgroups *
      params[i].inchannels * ksize * ksize;
    for (int j = 0; j < size; ++j) {
      std::cout<<this->sw_results[j]<<" "<<this->hw_results[j]<<std::endl;
      EXPECT_TRUE(checkEQ(this->sw_results[j], this->hw_results[j], 1e-1,
            1e-1));
    }
    clReleaseMemObject(this->ocl_input);
    clReleaseMemObject(this->ocl_weights);
    clReleaseMemObject(this->ocl_output);
    clReleaseMemObject(this->ocl_bias);
    clReleaseMemObject(this->ocl_relu_vals);
    clReleaseMemObject(this->ocl_params);
  }
}

