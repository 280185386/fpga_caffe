#include <vector>

#include "caffe/filler.hpp"
#include "caffe/layer.hpp"
#include "caffe/util/im2col.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/vision_layers.hpp"

namespace caffe {

template <typename Dtype>
void ConvolutionLayer<Dtype>::compute_output_shape() {
  this->height_out_ = (this->height_ + 2 * this->pad_h_ - this->kernel_h_)
      / this->stride_h_ + 1;
  this->width_out_ = (this->width_ + 2 * this->pad_w_ - this->kernel_w_)
      / this->stride_w_ + 1;
}

template <typename Dtype>
void ConvolutionLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
  const Dtype* weight = this->blobs_[0]->cpu_data();
  for (int i = 0; i < bottom.size(); ++i) {
    const Dtype* bottom_data = bottom[i]->cpu_data();
    Dtype* top_data = top[i]->mutable_cpu_data();
    for (int n = 0; n < this->num_; ++n) {
      this->forward_cpu_gemm(bottom_data + bottom[i]->offset(n), weight,
          top_data + top[i]->offset(n));
      if (this->bias_term_) {
        const Dtype* bias = this->blobs_[1]->cpu_data();
        this->forward_cpu_bias(top_data + top[i]->offset(n), bias);
      }
    }
  }
}

template <typename Dtype>
void ConvolutionLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
  const Dtype* weight = this->blobs_[0]->cpu_data();
  Dtype* weight_diff = this->blobs_[0]->mutable_cpu_diff();
  if (this->param_propagate_down_[0]) {
    caffe_set(this->blobs_[0]->count(), Dtype(0), weight_diff);
  }
  if (this->bias_term_ && this->param_propagate_down_[1]) {
    caffe_set(this->blobs_[1]->count(), Dtype(0),
        this->blobs_[1]->mutable_cpu_diff());
  }
  for (int i = 0; i < top.size(); ++i) {
    const Dtype* top_diff = top[i]->cpu_diff();
    const Dtype* bottom_data = bottom[i]->cpu_data();
    Dtype* bottom_diff = bottom[i]->mutable_cpu_diff();
    // Bias gradient, if necessary.
    if (this->bias_term_ && this->param_propagate_down_[1]) {
      Dtype* bias_diff = this->blobs_[1]->mutable_cpu_diff();
      for (int n = 0; n < this->num_; ++n) {
        this->backward_cpu_bias(bias_diff, top_diff + top[i]->offset(n));
      }
    }
    if (this->param_propagate_down_[0] || propagate_down[i]) {
      for (int n = 0; n < this->num_; ++n) {
        // gradient w.r.t. weight. Note that we will accumulate diffs.
        if (this->param_propagate_down_[0]) {
          this->weight_cpu_gemm(bottom_data + bottom[i]->offset(n),
              top_diff + top[i]->offset(n), weight_diff);
        }
        // gradient w.r.t. bottom data, if necessary.
        if (propagate_down[i]) {
          this->backward_cpu_gemm(top_diff + top[i]->offset(n), weight,
              bottom_diff + bottom[i]->offset(n));
        }
      }
    }
  }
}

#ifdef USE_OCL
template <>
void ConvolutionLayer<float>::Call_ocl(const vector<Blob<float>*>& bottom,
    const vector<Blob<float>*>& top) {
  
  const vector<shared_ptr<Blob<float> > > weight = this->blobs_;
  const float* weight_data = weight[0]->ocl_data();

  const float* in_data = bottom[0]->ocl_data();
  float *out_data = top[0]->mutable_ocl_data();

  clSetKernelArg(oclKernel[0], 0, sizeof(cl_mem), (const void *)&in_data);
	clSetKernelArg(oclKernel[0], 1, sizeof(cl_mem), (const void *)&out_data);
  clSetKernelArg(oclKernel[0], 2, sizeof(cl_mem), (const void *)&weight_data);

  size_t global_work_size[1] = {1};

  clEnqueueNDRangeKernel(oclCommandQueue, oclKernel[0], 1, NULL, 
      global_work_size, NULL, 0, NULL, NULL);
}

template <>
void ConvolutionLayer<double>::Call_ocl(const vector<Blob<double>*>& bottom,
    const vector<Blob<double>*>& top) {

  const vector<shared_ptr<Blob<double> > > weight = this->blobs_;
  const double* weight_data = weight[0]->ocl_data();

  const double* in_data = bottom[0]->ocl_data();
  double *out_data = top[0]->mutable_ocl_data();
  
  clSetKernelArg(oclKernel[1], 0, sizeof(cl_mem), (const void *)&in_data);
	clSetKernelArg(oclKernel[1], 1, sizeof(cl_mem), (const void *)&out_data);
  clSetKernelArg(oclKernel[1], 2, sizeof(cl_mem), (const void *)&weight_data);
  
  size_t global_work_size[1] = {1};

  clEnqueueNDRangeKernel(oclCommandQueue, oclKernel[1], 1, NULL, 
      global_work_size, NULL, 0, NULL, NULL);
}

template <typename Dtype>
void ConvolutionLayer<Dtype>::Forward_ocl(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {

  Call_ocl(bottom, bottom);
  
  const vector<shared_ptr<Blob<Dtype> > > weight = this->blobs_;
  const Dtype* weight_data = weight[0]->cpu_data();

  int groups = this->group_;
  int kernel_h = this->kernel_h_;
  int kernel_w = this->kernel_w_;
  int pad_h = this->pad_h_;
  int pad_w = this->pad_w_;
  int stride_h = this->stride_h_;
  int stride_w = this->stride_w_;
  int o_head, k_head;
  int o_g, k_g;
  const Dtype* in_data;
  Dtype* out_data;
  
  for (int i = 0; i < bottom.size(); ++i) {
    o_g = top[i]->channels()/groups;
    k_g = bottom[i]->channels()/groups;
    in_data = bottom[i]->cpu_data();
    out_data = top[i]->mutable_cpu_data();

    for(int j = 0; j < top[i]->count(); j++)
      out_data[j] = 0;
    for (int n = 0; n < top[i]->num(); n++) {
      for (int g = 0; g < groups; g++) {
        o_head = o_g * g; 
        k_head = k_g * g;
        for (int o = 0; o < o_g; o++) {
          for (int k = 0; k < k_g; k++) {
            for (int y = 0; y < top[i]->height(); y++) {
              for (int x = 0; x < top[i]->width(); x++) {
                for (int p = 0; p < kernel_h; p++) {
                  for (int q = 0; q < kernel_w; q++) {
                    int in_y = y * stride_h - pad_h + p;
                    int in_x = x * stride_w - pad_w + q;
                    if (in_y >= 0 && in_y < bottom[i]->height()
                      && in_x >= 0 && in_x < bottom[i]->width()) {
                          out_data[top[i]->offset(n, o + o_head, y, x)] +=
                            in_data[bottom[i]->offset(n, k + k_head, in_y, in_x)]
                            * weight_data[weight[0]->offset(o + o_head, k, p, q)];
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
    // Bias
    if (this->bias_term_) {
      const Dtype* bias_data = weight[1]->cpu_data();
      for (int n = 0; n < top[i]->num(); n++) {
        for (int o = 0; o < top[i]->channels(); o++) {
          for (int y = 0; y < top[i]->height(); y++) {
            for (int x = 0; x < top[i]->width(); x++) {
              out_data[top[i]->offset(n, o, y, x)] += bias_data[o];
            }
          }
        }
      }
    }
  }
}

#endif

#ifdef CPU_ONLY
STUB_GPU(ConvolutionLayer);
#endif

INSTANTIATE_CLASS(ConvolutionLayer);

}  // namespace caffe
