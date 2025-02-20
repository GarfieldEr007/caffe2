#ifndef CAFFE2_CORE_COMMON_GPU_H_
#define CAFFE2_CORE_COMMON_GPU_H_

#include <cublas_v2.h>
#include <cuda.h>
#include <cuda_runtime.h>
#include <curand.h>
#include <driver_types.h>  // cuda driver types

#include "caffe2/core/logging.h"
#include "caffe2/core/common.h"

namespace caffe2 {

/**
 * Check if the current running session has a cuda gpu present.
 *
 * Note that this is different from having caffe2 built with cuda. Building
 * Caffe2 with cuda only guarantees that this function exists. If there are no
 * cuda gpus present in the machine, or there are hardware configuration
 * problems like an insufficient driver, this function will still return false,
 * meaning that there is no usable GPU present.
 */
bool HasCudaGPU();

/**
 * Sets the default GPU id for Caffe2.
 *
 * If an operator is set to run on Cuda GPU but no gpu id is given, we will use
 * the default gpu id to run the operator. Before this function is explicitly
 * called, GPU 0 will be the default GPU id.
 */
void SetDefaultGPUID(const int deviceid);

/**
 * Gets the default GPU id for Caffe2.
 */
int GetDefaultGPUID();

/**
 * Runs a device query function and prints out the results to CAFFE_LOG_INFO.
 */
void DeviceQuery(const int deviceid);

/**
 * Return a peer access pattern by returning a matrix (in the format of a
 * nested vector) of boolean values specifying whether peer access is possible.
 *
 * This function returns false if anything wrong happens during the query of
 * the GPU access pattern.
 */
bool GetCudaPeerAccessPattern(vector<vector<bool> >* pattern);

namespace internal {
const char* cublasGetErrorString(cublasStatus_t error);
const char* curandGetErrorString(curandStatus_t error);
}  // namespace internal

// CUDA: various checks for different function calls.
#define CUDA_CHECK(condition)                                                  \
  do {                                                                         \
    cudaError_t error = condition;                                             \
    CAFFE_CHECK_EQ(error, cudaSuccess)                                         \
        << "Error at: " << __FILE__ << ":" << __LINE__ << ": "                 \
        << cudaGetErrorString(error);                                          \
  } while (0)

#define CUBLAS_CHECK(condition)                                                \
  do {                                                                         \
    cublasStatus_t status = condition;                                         \
    CAFFE_CHECK_EQ(status, CUBLAS_STATUS_SUCCESS)                              \
        << "Error at: " << __FILE__ << ":" << __LINE__ << ": "                 \
        << ::caffe2::internal::cublasGetErrorString(status);                   \
  } while (0)

#define CURAND_CHECK(condition)                                                \
  do {                                                                         \
    curandStatus_t status = condition;                                         \
    CAFFE_CHECK_EQ(status, CURAND_STATUS_SUCCESS)                              \
        << "Error at: " << __FILE__ << ":" << __LINE__ << ": "                 \
        << ::caffe2::internal::curandGetErrorString(status);                   \
  } while (0)

#define CUDA_1D_KERNEL_LOOP(i, n)                                              \
  for (int i = blockIdx.x * blockDim.x + threadIdx.x;                          \
       i < (n);                                                                \
       i += blockDim.x * gridDim.x)

// The following helper functions are here so that you can write a kernel call
// when you are not particularly interested in maxing out the kernels'
// performance. Usually, this will give you a reasonable speed, but if you
// really want to find the best performance, it is advised that you tune the
// size of the blocks and grids more reasonably.
// A legacy note: this is derived from the old good Caffe days, when I simply
// hard-coded the number of threads and wanted to keep backward compability for
// different computation capabilities.
// For more info on CUDA compute capabilities, visit the NVidia website at:
//    http://docs.nvidia.com/cuda/cuda-c-programming-guide/index.html#compute-capabilities

// The number of cuda threads to use. 512 is used for backward compatibility,
// and it is observed that setting it to 1024 usually does not bring much
// performance gain (which makes sense, because warp size being 32 means that
// blindly setting a huge block for a random kernel isn't optimal).
constexpr int CAFFE_CUDA_NUM_THREADS = 512;
// The maximum number of blocks to use in the default kernel call. We set it to
// 4096 which would work for compute capability 2.x (where 65536 is the limit).
// This number is very carelessly chosen. Ideally, one would like to look at
// the hardware at runtime, and pick the number of blocks that makes most
// sense for the specific runtime environment. This is a todo item.
constexpr int CAFFE_MAXIMUM_NUM_BLOCKS = 4096;

/**
 * @brief Compute the number of blocks needed to run N threads.
 */
inline int CAFFE_GET_BLOCKS(const int N) {
  return std::min((N + CAFFE_CUDA_NUM_THREADS - 1) / CAFFE_CUDA_NUM_THREADS,
                  CAFFE_MAXIMUM_NUM_BLOCKS);
}

}  // namespace caffe2
#endif  // CAFFE2_CORE_COMMON_GPU_H_
