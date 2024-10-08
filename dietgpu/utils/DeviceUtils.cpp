/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

#include "dietgpu/utils/DeviceUtils.h"
#include <hip/hip_runtime_api.h>
#include <mutex>
#include <unordered_map>
#include <cassert> 
#include <iostream>

namespace dietgpu {

//std::string errorToString(hipError_t err) {
//  return std::string(hipGetErrorString(err));
//}

//std::string errorToName(hipError_t err) {
//  return std::string(hipGetErrorName(err));
//}

int getCurrentDevice() {
  int dev = -1;
  CUDA_VERIFY(hipGetDevice(&dev));
//  CHECK_NE(dev, -1);
  assert(dev != -1 && "dev should not be equal to -1");
  return dev;
}

void setCurrentDevice(int device) {
  CUDA_VERIFY(hipSetDevice(device));
}

int getNumDevices() {
  int numDev = -1;
  hipError_t err = hipGetDeviceCount(&numDev);
  if (hipErrorNoDevice == err) {
    numDev = 0;
  } else {
    CUDA_VERIFY(err);
  }
//  CHECK_NE(numDev, -1);
  assert(numDev != -1 && "numDev should not be equal to -1");
  return numDev;
}

void profilerStart() {
  CUDA_VERIFY(hipProfilerStart());
}

void profilerStop() {
  CUDA_VERIFY(hipProfilerStop());
}

void synchronizeAllDevices() {
  for (int i = 0; i < getNumDevices(); ++i) {
    DeviceScope scope(i);

    CUDA_VERIFY(hipDeviceSynchronize());
  }
}

const hipDeviceProp_t& getDeviceProperties(int device) {
  static std::mutex mutex;
  static std::unordered_map<int, hipDeviceProp_t> properties;

  std::lock_guard<std::mutex> guard(mutex);

  auto it = properties.find(device);
  if (it == properties.end()) {
    hipDeviceProp_t prop;
    CUDA_VERIFY(hipGetDeviceProperties(&prop, device));

    properties[device] = prop;
    it = properties.find(device);
  }

  return it->second;
}

const hipDeviceProp_t& getCurrentDeviceProperties() {
  return getDeviceProperties(getCurrentDevice());
}

int getMaxThreads(int device) {
  return getDeviceProperties(device).maxThreadsPerBlock;
}

int getMaxThreadsCurrentDevice() {
  return getMaxThreads(getCurrentDevice());
}

size_t getMaxSharedMemPerBlock(int device) {
  return getDeviceProperties(device).sharedMemPerBlock;
}

size_t getMaxSharedMemPerBlockCurrentDevice() {
  return getMaxSharedMemPerBlock(getCurrentDevice());
}

int getDeviceForAddress(const void* p) {
  if (!p) {
    return -1;
  }

  hipPointerAttribute_t att;
  hipError_t err = hipPointerGetAttributes(&att, p);
//  CHECK(err == hipSuccess || err == hipErrorInvalidValue)
//      << "unknown error " << (int)err;
  assert(err == hipSuccess || err == hipErrorInvalidValue && "unknown error");
  if (!(err == hipSuccess || err == hipErrorInvalidValue)) {
    std::cerr << "unknown error " << static_cast<int>(err) << std::endl;
  }

  if (err == hipErrorInvalidValue) {
    // Make sure the current thread error status has been reset
    err = hipGetLastError();
//    CHECK_EQ(err, hipErrorInvalidValue) << "unknown error " << (int)err;
    assert(err == hipErrorInvalidValue && "Expected hipErrorInvalidValue, but got a different error");
    if (err != hipErrorInvalidValue) {
    std::cerr << "unknown error " << static_cast<int>(err) << std::endl;
    }
    return -1;
  }

  // memoryType is deprecated for CUDA 10.0+
#if CUDA_VERSION < 10000
  if (att.memoryType == hipMemoryTypeHost) {
    return -1;
  } else {
    return att.device;
  }
#else
  // FIXME: what to use for managed memory?
  if (att.type == hipMemoryTypeDevice) {
    return att.device;
  } else {
    return -1;
  }
#endif
}

bool getFullUnifiedMemSupport(int device) {
  const auto& prop = getDeviceProperties(device);
  return (prop.major >= 6);
}

bool getFullUnifiedMemSupportCurrentDevice() {
  return getFullUnifiedMemSupport(getCurrentDevice());
}

DeviceScope::DeviceScope(int device) {
  if (device >= 0) {
    int curDevice = getCurrentDevice();

    if (curDevice != device) {
      prevDevice_ = curDevice;
      setCurrentDevice(device);
      return;
    }
  }

  // Otherwise, we keep the current device
  prevDevice_ = -1;
}

DeviceScope::~DeviceScope() {
  if (prevDevice_ != -1) {
    setCurrentDevice(prevDevice_);
  }
}

CudaEvent::CudaEvent(hipStream_t stream, bool timer) : event_(nullptr) {
  CUDA_VERIFY(hipEventCreateWithFlags(
      &event_, timer ? hipEventDefault : hipEventDisableTiming));
  CUDA_VERIFY(hipEventRecord(event_, stream));
}

CudaEvent::CudaEvent(CudaEvent&& event) noexcept
    : event_(std::move(event.event_)) {
  event.event_ = nullptr;
}

CudaEvent::~CudaEvent() {
  if (event_) {
    CUDA_VERIFY(hipEventDestroy(event_));
  }
}

CudaEvent& CudaEvent::operator=(CudaEvent&& event) noexcept {
  event_ = std::move(event.event_);
  event.event_ = nullptr;

  return *this;
}

void CudaEvent::streamWaitOnEvent(hipStream_t stream) {
  CUDA_VERIFY(hipStreamWaitEvent(stream, event_, 0));
}

void CudaEvent::cpuWaitOnEvent() {
  CUDA_VERIFY(hipEventSynchronize(event_));
}

float CudaEvent::timeFrom(CudaEvent& from) {
  cpuWaitOnEvent();
  float ms = 0;
  CUDA_VERIFY(hipEventElapsedTime(&ms, from.event_, event_));

  return ms;
}

CudaStream::CudaStream(int flags) : stream_(nullptr) {
  CUDA_VERIFY(hipStreamCreateWithFlags(&stream_, flags));
}

CudaStream::CudaStream(CudaStream&& stream) noexcept
    : stream_(std::move(stream.stream_)) {
  stream.stream_ = nullptr;
}

CudaStream::~CudaStream() {
  if (stream_) {
    CUDA_VERIFY(hipStreamDestroy(stream_));
  }
}

CudaStream& CudaStream::operator=(CudaStream&& stream) noexcept {
  stream_ = std::move(stream.stream_);
  stream.stream_ = nullptr;

  return *this;
}

CudaStream CudaStream::make() {
  return CudaStream();
}

CudaStream CudaStream::makeNonBlocking() {
  return CudaStream(hipStreamNonBlocking);
}

} // namespace dietgpu
