#include <vector>

#include "caffe/filler.hpp"
#include "caffe/layers/ocl_pooling_hwcn_layer.hpp"

namespace caffe {

#ifdef USE_OCL

template <typename Dtype>
void OCLPoolingHWCNLayer<Dtype>::LayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  PoolingParameter pool_param = this->layer_param_.pooling_param();
  if (pool_param.global_pooling()) {
    CHECK(!(pool_param.has_kernel_size() ||
      pool_param.has_kernel_h() || pool_param.has_kernel_w()))
      << "With Global_pooling: true Filter size cannot specified";
  } else {
    CHECK(!pool_param.has_kernel_size() !=
      !(pool_param.has_kernel_h() && pool_param.has_kernel_w()))
      << "Filter size is kernel_size OR kernel_h and kernel_w; not both";
    CHECK(pool_param.has_kernel_size() ||
      (pool_param.has_kernel_h() && pool_param.has_kernel_w()))
      << "For non-square filters both kernel_h and kernel_w are required.";
  }
  CHECK((!pool_param.has_pad() && pool_param.has_pad_h()
      && pool_param.has_pad_w())
      || (!pool_param.has_pad_h() && !pool_param.has_pad_w()))
      << "pad is pad OR pad_h and pad_w are required.";
  CHECK((!pool_param.has_stride() && pool_param.has_stride_h()
      && pool_param.has_stride_w())
      || (!pool_param.has_stride_h() && !pool_param.has_stride_w()))
      << "Stride is stride OR stride_h and stride_w are required.";
  this->global_pooling_ = pool_param.global_pooling();
  if (pool_param.has_kernel_size()) {
    this->kernel_h_ = this->kernel_w_ = pool_param.kernel_size();
  } else {
    this->kernel_h_ = pool_param.kernel_h();
    this->kernel_w_ = pool_param.kernel_w();
  }
  CHECK_GT(this->kernel_h_, 0) << "Filter dimensions cannot be zero.";
  CHECK_GT(this->kernel_w_, 0) << "Filter dimensions cannot be zero.";
  if (!pool_param.has_pad_h()) {
    this->pad_h_ = this->pad_w_ = pool_param.pad();
  } else {
    this->pad_h_ = pool_param.pad_h();
    this->pad_w_ = pool_param.pad_w();
  }
  if (!pool_param.has_stride_h()) {
    this->stride_h_ = this->stride_w_ = pool_param.stride();
  } else {
    this->stride_h_ = pool_param.stride_h();
    this->stride_w_ = pool_param.stride_w();
  }
  if (this->pad_h_ != 0 || this->pad_w_ != 0) {
    CHECK(this->layer_param_.pooling_param().pool()
        == PoolingParameter_PoolMethod_AVE
        || this->layer_param_.pooling_param().pool()
        == PoolingParameter_PoolMethod_MAX)
        << "Padding implemented only for average and max pooling.";
    CHECK_LT(this->pad_h_, this->kernel_h_);
    CHECK_LT(this->pad_w_, this->kernel_w_);
  }

  CRParameter cr_param = this->layer_param_.cr_param(); 
  kernel_params *forward_params = &ocl_params_;
  int num_ = bottom[0]->shape(3); 
  forward_params->ydim = bottom[0]->shape(0);
  forward_params->xdim = bottom[0]->shape(1);
  forward_params->inchannels = bottom[0]->shape(2);
  forward_params->outchannels = 1;
  forward_params->numimages = num_;
  forward_params->ksize = 3;

  int burstchannels_ = 16;

  if (burstchannels_ > forward_params->inchannels) {
    burstchannels_ = forward_params->inchannels;
  } else {
    int tchannel = burstchannels_;
    while (forward_params->inchannels % tchannel != 0)
      tchannel--;
    burstchannels_ = tchannel;
  }
  forward_params->backward = 0;
  forward_params->rpofm = 0;
  forward_params->xtile_pad = 0;
  forward_params->burstydim = 0;
  forward_params->stride = 1;
  forward_params->pad = 1;
  forward_params->burstchannels = burstchannels_;
  forward_params->rpo = forward_params->inchannels / burstchannels_;
  forward_params->numgroups = 1;
  forward_params->fc = 0;
  forward_params->relu = 0;
  forward_params->pool = 1;
  forward_params->pksize = this->kernel_h_;

  // Backward params
  kernel_params *backward_params = &ocl_params_bi_;
  backward_params->ydim = bottom[0]->shape(0);
  backward_params->xdim = bottom[0]->shape(1);
  backward_params->inchannels = forward_params->inchannels;
  backward_params->outchannels = 1;
  backward_params->ksize = 3;
  backward_params->numimages = num_;
  backward_params->rpofm = 0;
  backward_params->xtile_pad = 0;
  backward_params->burstydim = 0;
  backward_params->stride = 1;
  backward_params->pad = 1;
  backward_params->burstchannels = burstchannels_;
  backward_params->rpo = backward_params->inchannels / burstchannels_;
  backward_params->numgroups = 1;
  backward_params->fc = 0;
  backward_params->relu = 0;
  backward_params->pool = 1;
  backward_params->backward = 1;
  backward_params->pksize = this->kernel_h_;
}

template <typename Dtype>
void OCLPoolingHWCNLayer<Dtype>::Reshape(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  CHECK_EQ(4, bottom[0]->num_axes()) << "Input must have 4 axes, "
      << "corresponding to (num, channels, height, width)";
  this->num_ = bottom[0]->shape(3);
  this->channels_ = bottom[0]->shape(2);
  this->height_ = bottom[0]->shape(0);
  this->width_ = bottom[0]->shape(1);
  this->pooled_height_ = static_cast<int>(ceil(static_cast<float>(
      this->height_ + 2 * this->pad_h_ - this->kernel_h_) / this->stride_h_))
      + 1;
  this->pooled_width_ = static_cast<int>(ceil(static_cast<float>(
      this->width_ + 2 * this->pad_w_ - this->kernel_w_) / this->stride_w_))
      + 1;
 
  top[0]->Reshape(this->pooled_height_, this->pooled_width_, this->channels_,
      bottom[0]->shape(3));

  relu_indices.Reshape(this->pooled_height_, this->pooled_width_,
      this->channels_, bottom[0]->shape(3) / 2);
  weights_placeholder.Reshape(1, 1, 1, 1);
  bias_placeholder.Reshape(1, 1, 1, 1);
}

template <typename Dtype>
void OCLPoolingHWCNLayer<Dtype>::Forward_ocl(
    const vector<Blob<Dtype>*>& bottom, const vector<Blob<Dtype>*>& top) {
  kernel_params *params = &ocl_params_;

  vector<int> shape(1);
  shape[0] = sizeof(kernel_params) / sizeof(int);
  param_vals.Reshape(shape);

  int *pool_params = param_vals.mutable_cpu_data();

  for (int i = 0; i < shape[0]; ++i) {
    pool_params[i] = ((int *)params)[i];
  }
  
  const int* p_params = param_vals.ocl_data();

  size_t insize = sizeof(chalf) * bottom[0]->count();
  size_t outsize = sizeof(chalf) * top[0]->count();
  std::vector<cl_event> events;
  int events_size = 1;

  const chalf *bias_data = bias_placeholder.ocl_data();
  const chalf *weight_data = weights_placeholder.ocl_data();
  chalf *top_data;
  int *relu_vals;

  for (int i = 0; i < bottom.size(); i++) {
    events.resize(events_size, 0);
    const chalf* bottom_data =
      reinterpret_cast<const chalf *>(bottom[i]->ocl_data(insize));
    top_data = reinterpret_cast<chalf *>(top[i]->mutable_ocl_data(0));
    relu_vals = relu_indices.mutable_ocl_data(0);
    clSetKernelArg(this->ocl_kernel, 0, sizeof(cl_mem),
      (const void *)&bottom_data);
    clSetKernelArg(this->ocl_kernel, 1, sizeof(cl_mem),
      (const void *)&weight_data);
    clSetKernelArg(this->ocl_kernel, 2, sizeof(cl_mem),
      (const void *)&bias_data);
    clSetKernelArg(this->ocl_kernel, 3, sizeof(cl_mem),
      (const void *)&top_data);
    clSetKernelArg(this->ocl_kernel, 4, sizeof(cl_mem),
      (const void *)&relu_vals);
    clSetKernelArg(this->ocl_kernel, 5, sizeof(cl_mem),
      (const void *)&p_params);
    int g = 0;
    clSetKernelArg(this->ocl_kernel, 6, sizeof(cl_int),
        (const void *)&g);
    clEnqueueTask(oclCommandQueue, this->ocl_kernel, 0, NULL, &(events[0]));
    clWaitForEvents(events.size(), events.data());
  }
}

template <typename Dtype>
void OCLPoolingHWCNLayer<Dtype>::Backward_ocl(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
  kernel_params *params = &ocl_params_bi_;
  vector<int> shape(1);
  shape[0] = sizeof(kernel_params) / sizeof(int);
  param_vals.Reshape(shape);

  int *pool_params = param_vals.mutable_cpu_data();

  for (int i = 0; i < shape[0]; ++i) {
    pool_params[i] = ((int *)params)[i];
  }
  
  const int* p_params_b = param_vals.ocl_data();

  size_t insize = sizeof(chalf) * bottom[0]->count();
  size_t outsize = sizeof(chalf) * top[0]->count();
  std::vector<cl_event> events;

  int events_size = 1;

  const chalf *bias_data = bias_placeholder.ocl_data();
  const chalf *weight_data = weights_placeholder.ocl_data();
  const chalf *top_diff;
  const int *relu_vals;

  chalf *bottom_diff =
    reinterpret_cast<chalf *>(bottom[0]->mutable_cpu_diff());
  for (int i = 0; i < bottom[0]->count(); ++i)
    bottom_diff[i] = chalf(0);
  for (int i = 0; i < bottom.size(); i++) {
    events.resize(events_size, 0);
    chalf *bottom_diff =
      reinterpret_cast<chalf *>(bottom[i]->mutable_ocl_diff(1, insize));
    top_diff = reinterpret_cast<const chalf *>(top[i]->ocl_diff(outsize));
    relu_vals = relu_indices.ocl_data();
    clSetKernelArg(this->ocl_kernel, 0, sizeof(cl_mem),
      (const void *)&top_diff);
    clSetKernelArg(this->ocl_kernel, 1, sizeof(cl_mem),
      (const void *)&weight_data);
    clSetKernelArg(this->ocl_kernel, 2, sizeof(cl_mem),
      (const void *)&bias_data);
    clSetKernelArg(this->ocl_kernel, 3, sizeof(cl_mem),
      (const void *)&bottom_diff);
    clSetKernelArg(this->ocl_kernel, 4, sizeof(cl_mem),
      (const void *)&relu_vals);
    clSetKernelArg(this->ocl_kernel, 5, sizeof(cl_mem),
      (const void *)&p_params_b);
    int g = 0;
    clSetKernelArg(this->ocl_kernel, 6, sizeof(cl_int),
        (const void *)&g);
    clEnqueueTask(oclCommandQueue, this->ocl_kernel, 0, NULL, &(events[0]));
    clWaitForEvents(events.size(), events.data());
  }
}


INSTANTIATE_CLASS(OCLPoolingHWCNLayer);
REGISTER_LAYER_CLASS(OCLPoolingHWCN);
#endif
}  // namespace caffe
