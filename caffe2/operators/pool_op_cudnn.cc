#include "caffe2/core/common_cudnn.h"
#include "caffe2/core/context_gpu.h"
#include "caffe2/operators/conv_pool_op_base.h"

namespace caffe2 {

template <typename T>
class CuDNNPoolOp : public ConvPoolOpBase<CUDAContext> {
 public:
  CuDNNPoolOp(const OperatorDef& operator_def, Workspace* ws)
      : ConvPoolOpBase<CUDAContext>(operator_def, ws),
        cudnn_wrapper_(&device_context_) {
    CUDNN_CHECK(cudnnCreateTensorDescriptor(&bottom_desc_));
    CUDNN_CHECK(cudnnCreateTensorDescriptor(&top_desc_));
    CUDNN_CHECK(cudnnCreatePoolingDescriptor(&pooling_desc_));
    // Figure out the pooling descriptor.
    if (def().type().substr(0, 7) == "MaxPool") {
      mode_ = CUDNN_POOLING_MAX;
    } else if (def().type().substr(0, 11) == "AveragePool") {
      mode_ = CUDNN_POOLING_AVERAGE_COUNT_EXCLUDE_PADDING;
    } else {
      CAFFE_LOG_FATAL << "Unsupported pooling method: " << def().type();
    }
  }

  ~CuDNNPoolOp() {
    CUDNN_CHECK(cudnnDestroyTensorDescriptor(bottom_desc_));
    CUDNN_CHECK(cudnnDestroyTensorDescriptor(top_desc_));
    CUDNN_CHECK(cudnnDestroyPoolingDescriptor(pooling_desc_));
  }

  bool RunOnDevice() final {
    auto& X = Input(0);
    auto* Y = Output(0);
    int N, C, H, W;
    switch (order_) {
    case StorageOrder::NHWC:
      N = X.dim(0); H = X.dim(1); W = X.dim(2); C = X.dim(3);
      break;
    case StorageOrder::NCHW:
      N = X.dim(0); C = X.dim(1); H = X.dim(2); W = X.dim(3);
      break;
    default:
      CAFFE_LOG_FATAL << "Unknown storage order: " << order_;
    }
    ConvPoolOpBase::SetOutputSize(X, Y, C);

    if (cudnn_input_dims_ != X.dims()) {
      // Dimensions changed; we will need to re-initialize things.
      CAFFE_VLOG(1) << "Changing the cudnn descriptor configurations.";
      cudnn_input_dims_ = X.dims();
      CUDNN_CHECK(cudnnSetTensor4dDescriptor(
          bottom_desc_, GetCudnnTensorFormat(order_),
          cudnnTypeWrapper<T>::type, N, C, H, W));
      CUDNN_CHECK(cudnnSetTensor4dDescriptor(
          top_desc_, GetCudnnTensorFormat(order_),
          cudnnTypeWrapper<T>::type, N, C,
          order_ == StorageOrder::NCHW ? Y->dim(2) : Y->dim(1),
          order_ == StorageOrder::NCHW ? Y->dim(3) : Y->dim(2)));
      if (pad_t_ != pad_l_ || pad_l_ != pad_r_) {
        CAFFE_LOG_FATAL << "Cudnn pooling only supports even padding on both sides.";
      }
      CUDNN_CHECK(cudnnSetPooling2dDescriptor(
          pooling_desc_, mode_, kernel_h_, kernel_w_, pad_t_, pad_l_,
          stride_h_, stride_w_));
    }
    // Carry out the pooling computation.
    CUDNN_CHECK(cudnnPoolingForward(
        cudnn_wrapper_.cudnn_handle(), pooling_desc_,
        cudnnTypeWrapper<T>::kOne(), bottom_desc_, X.template data<T>(),
        cudnnTypeWrapper<T>::kZero(), top_desc_,
        Y->template mutable_data<T>()));
    return true;
  }

 protected:
  vector<int> cudnn_input_dims_;

  CuDNNWrapper cudnn_wrapper_;
  cudnnTensorDescriptor_t bottom_desc_;
  cudnnTensorDescriptor_t top_desc_;
  cudnnPoolingDescriptor_t pooling_desc_;
  cudnnPoolingMode_t mode_;

  // Input: X
  // Output: Y
  INPUT_OUTPUT_STATS(1, 1, 1, 1);

  DISABLE_COPY_AND_ASSIGN(CuDNNPoolOp);
};

template <typename T>
class CuDNNPoolGradientOp : public ConvPoolOpBase<CUDAContext> {
 public:
  CuDNNPoolGradientOp(const OperatorDef& operator_def, Workspace* ws)
      : ConvPoolOpBase<CUDAContext>(operator_def, ws),
        cudnn_wrapper_(&device_context_) {
    CUDNN_CHECK(cudnnCreateTensorDescriptor(&bottom_desc_));
    CUDNN_CHECK(cudnnCreateTensorDescriptor(&top_desc_));
    CUDNN_CHECK(cudnnCreatePoolingDescriptor(&pooling_desc_));
    // Figure out the pooling descriptor.
    if (def().type() == "MaxPoolGradient") {
      mode_ = CUDNN_POOLING_MAX;
    } else if (def().type() == "AveragePoolGradient") {
      mode_ = CUDNN_POOLING_AVERAGE_COUNT_EXCLUDE_PADDING;
    } else {
      CAFFE_LOG_FATAL << "Unsupported pooling method: " << def().type();
    }
  }

  ~CuDNNPoolGradientOp() {
    CUDNN_CHECK(cudnnDestroyTensorDescriptor(bottom_desc_));
    CUDNN_CHECK(cudnnDestroyTensorDescriptor(top_desc_));
    CUDNN_CHECK(cudnnDestroyPoolingDescriptor(pooling_desc_));
  }

  bool RunOnDevice() final {
    auto& X = Input(0);
    auto& Y = Input(1);
    auto& dY = Input(2);
    auto* dX = Output(0);
    dX->ReshapeLike(X);
    int N, C, H, W;
    switch (order_) {
    case StorageOrder::NHWC:
      N = X.dim(0); H = X.dim(1); W = X.dim(2); C = X.dim(3);
      break;
    case StorageOrder::NCHW:
      N = X.dim(0); C = X.dim(1); H = X.dim(2); W = X.dim(3);
      break;
    default:
      CAFFE_LOG_FATAL << "Unknown storage order: " << order_;
    }
    ConvPoolOpBase<CUDAContext>::ComputePads(H, W);

    if (cudnn_input_dims_ != X.dims()) {
      // Dimensions changed; we will need to re-initialize things.
      CAFFE_VLOG(1) << "Changing the cudnn descriptor configurations.";
      cudnn_input_dims_ = X.dims();
      CUDNN_CHECK(cudnnSetTensor4dDescriptor(
          bottom_desc_, GetCudnnTensorFormat(order_),
          cudnnTypeWrapper<T>::type, N, C, H, W));
      CUDNN_CHECK(cudnnSetTensor4dDescriptor(
          top_desc_, GetCudnnTensorFormat(order_),
          cudnnTypeWrapper<T>::type, N, C,
          order_ == StorageOrder::NCHW ? Y.dim(2) : Y.dim(1),
          order_ == StorageOrder::NCHW ? Y.dim(3) : Y.dim(2)));
      if (pad_t_ != pad_l_ || pad_l_ != pad_r_) {
        CAFFE_LOG_FATAL << "Cudnn pooling only supports even padding on both sides.";
      }
      CUDNN_CHECK(cudnnSetPooling2dDescriptor(
          pooling_desc_, mode_, kernel_h_, kernel_w_, pad_t_, pad_l_,
          stride_h_, stride_w_));
    }
    // Carry out the pooling computation.
    CUDNN_CHECK(cudnnPoolingBackward(
        cudnn_wrapper_.cudnn_handle(), pooling_desc_,
        cudnnTypeWrapper<T>::kOne(), top_desc_,
        Y.template data<T>(), top_desc_, dY.template data<T>(),
        bottom_desc_, X.template data<T>(), cudnnTypeWrapper<T>::kZero(),
        bottom_desc_, dX->template mutable_data<T>()));
    return true;
  }

 protected:
  vector<int> cudnn_input_dims_;

  CuDNNWrapper cudnn_wrapper_;
  cudnnTensorDescriptor_t bottom_desc_;
  cudnnTensorDescriptor_t top_desc_;
  cudnnPoolingDescriptor_t pooling_desc_;
  cudnnPoolingMode_t mode_;

  // Input: X, Y, dY
  // Output: dX
  INPUT_OUTPUT_STATS(3, 3, 1, 1);
  DISABLE_COPY_AND_ASSIGN(CuDNNPoolGradientOp);
};

namespace {
REGISTER_CUDNN_OPERATOR(AveragePool, CuDNNPoolOp<float>);
REGISTER_CUDNN_OPERATOR(AveragePoolGradient, CuDNNPoolGradientOp<float>);
REGISTER_CUDNN_OPERATOR(MaxPool, CuDNNPoolOp<float>);
REGISTER_CUDNN_OPERATOR(MaxPoolGradient, CuDNNPoolGradientOp<float>);

#if CUDNN_VERSION >= 3000
REGISTER_CUDNN_OPERATOR(AveragePoolFp16, CuDNNPoolOp<float16>);
REGISTER_CUDNN_OPERATOR(AveragePoolFp16Gradient, CuDNNPoolGradientOp<float16>);
REGISTER_CUDNN_OPERATOR(MaxPoolFp16, CuDNNPoolOp<float16>);
REGISTER_CUDNN_OPERATOR(MaxPoolFp16Gradient, CuDNNPoolGradientOp<float16>);


struct GetPoolGradient : public GetGradientDefBase {
  vector<OperatorDef>* Create(const OperatorDef& def) override {
    return SingleGradientDef(
        def.type() + "Gradient", "",
        vector<string>{I(def, 0), O(def, 0), GO(def, 0)},
        vector<string>{GI(def, 0)});
  }
};
REGISTER_GRADIENT(AveragePoolFp16, GetPoolGradient);
REGISTER_GRADIENT(MaxPoolFp16, GetPoolGradient);
#endif  // CUDNN_VERSION >= 3000


}  // namespace
}  // namespace caffe2
