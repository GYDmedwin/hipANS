/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#pragma once
#include <stdio.h>
//#include <hip/hip_runtime.h>
#include <hip/hip_runtime.h>
//#include <glog/logging.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <iostream>

#define PRINT_ERROR_LOCATION() \
    fprintf(stderr, "Error in file '%s' at line %d.\n", __FILE__, __LINE__);

/// Wrapper to test return status of CUDA functions
#define CUDA_VERIFY(X)                                         \
  do {                                                         \
    auto err__ = (X);                                          \
/*    CHECK_EQ(err__, hipSuccess)<< "CUDA error " << dietgpu::errorToName(err__) << " " << dietgpu::errorToString(err__);std::exit(EXIT_FAILURE);}
*/	if(err__ != hipSuccess)\
	{\
	PRINT_ERROR_LOCATION();                                         \
        fprintf(stderr, "HIP error: %s\n", hipGetErrorString(err__));\
        exit(EXIT_FAILURE);\
	}\
  } while (0)

#define CURAND_VERIFY(X)                                                     \
  do {                                                                       \
    auto err__ = (X);                                                        \
/*    CHECK_EQ(err__, HIPRAND_STATUS_SUCCESS) << "cuRAND error " << (int)err__;*/ \
	if(err__ != HIPRAND_STATUS_SUCCESS){\
	std::cout<< "cuRAND error " << (int)err__;\
		std::exit(EXIT_FAILURE);\
	}\
  } while (0)

#ifdef __CUDA_ARCH__
#define GPU_ASSERT(X) assert(X)
#else
#define GPU_ASSERT(X) CHECK(X)
#endif // __CUDA_ARCH__

/// Wrapper to synchronously probe for CUDA errors
// #define GPU_SYNC_ERROR 1

#ifdef GPU_SYNC_ERROR
#define CUDA_TEST_ERROR()                 \
  do {                                    \
    CUDA_VERIFY(hipDeviceSynchronize()); \
  } while (0)
#else
#define CUDA_TEST_ERROR()            \
  do {                               \
    CUDA_VERIFY(hipGetLastError()); \
  } while (0)
#endif

namespace dietgpu {

/// std::string wrapper around hipGetErrorString
//std::string errorToString(hipError_t err);

/// std::string wrapper around hipGetErrorName
//std::string errorToName(hipError_t err);

/// Returns the current thread-local GPU device
int getCurrentDevice();

/// Sets the current thread-local GPU device
void setCurrentDevice(int device);

/// Returns the number of available GPU devices
int getNumDevices();

/// Starts the CUDA profiler (exposed via SWIG)
void profilerStart();

/// Stops the CUDA profiler (exposed via SWIG)
void profilerStop();

/// Synchronizes the CPU against all devices (equivalent to
/// hipDeviceSynchronize for each device)
void synchronizeAllDevices();

/// Returns a cached hipDeviceProp_t for the given device
const hipDeviceProp_t& getDeviceProperties(int device);

/// Returns the cached hipDeviceProp_t for the current device
const hipDeviceProp_t& getCurrentDeviceProperties();

/// Returns the maximum number of threads available for the given GPU
/// device
int getMaxThreads(int device);

/// Equivalent to getMaxThreads(getCurrentDevice())
int getMaxThreadsCurrentDevice();

/// Returns the maximum smem available for the given GPU device
size_t getMaxSharedMemPerBlock(int device);

/// Equivalent to getMaxSharedMemPerBlock(getCurrentDevice())
size_t getMaxSharedMemPerBlockCurrentDevice();

/// For a given pointer, returns whether or not it is located on
/// a device (deviceId >= 0) or the host (-1).
int getDeviceForAddress(const void* p);

/// Does the given device support full unified memory sharing host
/// memory?
bool getFullUnifiedMemSupport(int device);

/// Equivalent to getFullUnifiedMemSupport(getCurrentDevice())
bool getFullUnifiedMemSupportCurrentDevice();

/// RAII object to set the current device, and restore the previous
/// device upon destruction
class DeviceScope {
 public:
  explicit DeviceScope(int device);
  ~DeviceScope();

 private:
  int prevDevice_;
};

// RAII object to manage a hipEvent_t
class CudaEvent {
 public:
  /// Creates an event and records it in this stream
  explicit CudaEvent(hipStream_t stream, bool timer = false);
  CudaEvent(const CudaEvent& event) = delete;
  CudaEvent(CudaEvent&& event) noexcept;
  ~CudaEvent();

  CudaEvent& operator=(CudaEvent&& event) noexcept;
  CudaEvent& operator=(CudaEvent& event) = delete;

  inline hipEvent_t get() {
    return event_;
  }

  /// Wait on this event in this stream
  void streamWaitOnEvent(hipStream_t stream);

  /// Have the CPU wait for the completion of this event
  void cpuWaitOnEvent();

  /// Returns the elapsed time from the other event
  float timeFrom(CudaEvent& from);

 private:
  hipEvent_t event_;
};

// RAII object to manage a hipStream_t
class CudaStream {
 public:
  /// Creates a stream on the current device
  CudaStream(int flags = hipStreamDefault);
  CudaStream(const CudaStream& stream) = delete;
  CudaStream(CudaStream&& stream) noexcept;
  ~CudaStream();

  CudaStream& operator=(CudaStream&& stream) noexcept;
  CudaStream& operator=(CudaStream& stream) = delete;

  inline hipStream_t get() {
    return stream_;
  }

  operator hipStream_t() {
    return stream_;
  }

  static CudaStream make();
  static CudaStream makeNonBlocking();

 private:
  hipStream_t stream_;
};

/// Call for a collection of streams to wait on
template <typename L1, typename L2>
void streamWaitBase(const L1& listWaiting, const L2& listWaitOn) {
  // For all the streams we are waiting on, create an event
  std::vector<hipEvent_t> events;
  for (auto& stream : listWaitOn) {
    hipEvent_t event;
    CUDA_VERIFY(hipEventCreateWithFlags(&event, hipEventDisableTiming));
    CUDA_VERIFY(hipEventRecord(event, stream));
    events.push_back(event);
  }

  // For all the streams that are waiting, issue a wait
  for (auto& stream : listWaiting) {
    for (auto& event : events) {
      CUDA_VERIFY(hipStreamWaitEvent(stream, event, 0));
    }
  }

  for (auto& event : events) {
    CUDA_VERIFY(hipEventDestroy(event));
  }
}

/// These versions allow usage of initializer_list as arguments, since
/// otherwise {...} doesn't have a type
template <typename L1>
void streamWait(const L1& a, const std::initializer_list<hipStream_t>& b) {
  streamWaitBase(a, b);
}

template <typename L2>
void streamWait(const std::initializer_list<hipStream_t>& a, const L2& b) {
  streamWaitBase(a, b);
}

inline void streamWait(
    const std::initializer_list<hipStream_t>& a,
    const std::initializer_list<hipStream_t>& b) {
  streamWaitBase(a, b);
}

} // namespace dietgpu
