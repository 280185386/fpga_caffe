#include <algorithm>
#include <vector>

#include "caffe/layer.hpp"
#include "caffe/vision_layers.hpp"

namespace caffe {

template <typename Dtype>
void ReLULayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  const Dtype* bottom_data = bottom[0]->cpu_data();
  Dtype* top_data = top[0]->mutable_cpu_data();
  const int count = bottom[0]->count();
  Dtype negative_slope = this->layer_param_.relu_param().negative_slope();

  for (int i = 0; i < count; ++i) {
    top_data[i] = std::max(bottom_data[i], Dtype(0))
        + negative_slope * std::min(bottom_data[i], Dtype(0));
  }
}

template <typename Dtype>
void ReLULayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
    const vector<bool>& propagate_down,
    const vector<Blob<Dtype>*>& bottom) {
  if (propagate_down[0]) {
    const Dtype* bottom_data = bottom[0]->cpu_data();
    const Dtype* top_diff = top[0]->cpu_diff();
    Dtype* bottom_diff = bottom[0]->mutable_cpu_diff();
    const int count = bottom[0]->count();
    Dtype negative_slope = this->layer_param_.relu_param().negative_slope();
    for (int i = 0; i < count; ++i) {
      bottom_diff[i] = top_diff[i] * ((bottom_data[i] > 0)
          + negative_slope * (bottom_data[i] <= 0));
    }
  }
}

#ifdef USE_OCL
template <typename Dtype>
void ReLULayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
        const vector <Blob<Dtype>*>& top) {                                                               
  NeuronLayer<Dtype>::LayerSetUp(bottom, top);   
}  

template <>
void ReLULayer<float>::Call_ocl(const vector<Blob<float>*>& bottom, 
    const vector<Blob<float>*>& top) {

  const float* bottom_data = bottom[0]->cpu_data();
  float* top_data = top[0]->mutable_cpu_data();
  int nodes = 3025;
  cl_int error; 

  cl_mem input = clCreateBuffer(oclContext, CL_MEM_READ_ONLY, 
     nodes*sizeof(float), NULL, &error);
  cl_mem output = clCreateBuffer(oclContext, CL_MEM_WRITE_ONLY, 
      nodes*sizeof(float), NULL, &error);

  cl_event event;

  error = clSetKernelArg(this->ocl_float_kernel, 0, sizeof(cl_mem),
      (const void *)&input);
  error = clSetKernelArg(this->ocl_float_kernel, 1, sizeof(cl_mem),
      (const void *)&output);
  
  int limit = std::ceil((bottom[0]->count())/(float)nodes);

  size_t cb_size; 
  if(limit == 0)
    limit = 1;
  for(int i = 0; i < limit; i++) {
    if(i != limit - 1)
      cb_size = nodes;
    else
      cb_size = bottom[0]->count() - (limit - 1) * nodes;
    error = clEnqueueWriteBuffer(oclCommandQueue, input, CL_TRUE, 0,
       cb_size * sizeof(float), (const void *)(bottom_data + i * nodes), NULL,
       NULL, NULL); 
    clEnqueueTask(oclCommandQueue, this->ocl_float_kernel, 0, NULL, NULL); 
    clEnqueueReadBuffer(oclCommandQueue, output, CL_TRUE, 0, 
       cb_size * sizeof(float), (void *) (top_data + i * nodes), NULL, NULL, 
       NULL);
  }
  clReleaseMemObject(input);
  clReleaseMemObject(output);
}

template <>
void ReLULayer<double>::Call_ocl(const vector<Blob<double>*>& bottom,
    const vector<Blob<double>*>& top) {
  Forward_cpu(bottom, top);
}

template <typename Dtype>
void ReLULayer<Dtype>::Forward_ocl(const vector<Blob<Dtype>*>& bottom,
    const vector<Blob<Dtype>*>& top) {
  cl_int error;
  std::string path(".build_release/opencl/src/caffe/layers/");
    
  const char *filename = (path+this->layer_param_.xcl_name()).c_str();
   
  char *sourceStr;
  size_t sourceSize = caffe::convertToString(filename, &sourceStr);
  this->ocl_layer_program = clCreateProgramWithBinary(oclContext, 1,
      &oclDevices, &sourceSize, (const unsigned char **)&sourceStr, NULL, 
      &error);
  clBuildProgram(this->ocl_layer_program, 0, NULL, NULL, NULL, &error);
  delete sourceStr;
  this->ocl_float_kernel = clCreateKernel(this->ocl_layer_program, 
      this->layer_param_.kernel_name().c_str(), &error);
  Call_ocl(bottom, top);
  clReleaseKernel(this->ocl_float_kernel);
  clReleaseProgram(this->ocl_layer_program);
}
#endif


#ifdef CPU_ONLY
STUB_GPU(ReLULayer);
#endif

INSTANTIATE_CLASS(ReLULayer);

}  // namespace caffe
