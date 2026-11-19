/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

#define LOG_TAG "sensors.ayn"

#include <android-base/logging.h>
#include <android-base/properties.h>
#include <dlfcn.h>

#include <cmath>
#include <string>
#include <vector>

#include <V2_1/SubHal.h>

using ::android::sp;
using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::RateLevel;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SharedMemInfo;
using ::android::hardware::sensors::V2_1::Event;
using ::android::hardware::sensors::V2_1::ISensors;
using ::android::hardware::sensors::V2_1::SensorInfo;
using ::android::hardware::sensors::V2_1::SensorType;
using ::android::hardware::sensors::V2_1::implementation::IHalProxyCallback;
using ::android::hardware::sensors::V2_1::implementation::ISensorsSubHal;

namespace {

constexpr const char* kRealSubHals[] = {"sensors.qsh.so", "sensors.ssc.so"};

/*
 * Rotates sensor axes so that they match the display frame after
 * ro.surface_flinger.primary_display_orientation is applied.
 */
class AxisRotation {
  public:
    explicit AxisRotation(int degrees) {
        const double a = degrees * M_PI / 180.0;
        mCos = static_cast<float>(std::round(std::cos(a)));
        mSin = static_cast<float>(std::round(std::sin(a)));
        mHalfCos = static_cast<float>(std::cos(a / 2));
        mHalfSin = static_cast<float>(std::sin(a / 2));
    }

    void apply(Event& event) const {
        float* d = event.u.data.data();
        switch (event.sensorType) {
            case SensorType::ACCELEROMETER:
            case SensorType::MAGNETIC_FIELD:
            case SensorType::GYROSCOPE:
            case SensorType::GRAVITY:
            case SensorType::LINEAR_ACCELERATION:
                rotateVec(&d[0]);
                break;
            case SensorType::ACCELEROMETER_UNCALIBRATED:
            case SensorType::MAGNETIC_FIELD_UNCALIBRATED:
            case SensorType::GYROSCOPE_UNCALIBRATED:
                rotateVec(&d[0]);
                rotateVec(&d[3]);
                break;
            case SensorType::ROTATION_VECTOR:
            case SensorType::GAME_ROTATION_VECTOR:
            case SensorType::GEOMAGNETIC_ROTATION_VECTOR:
                rotateQuat(&d[0]);
                break;
            default:
                break;
        }
    }

    void applyInverse(Event& event) const {
        AxisRotation inverse = *this;
        inverse.mSin = -mSin;
        inverse.mHalfSin = -mHalfSin;
        inverse.apply(event);
    }

  private:
    // v' = Rz(-a) * v
    void rotateVec(float* v) const {
        const float x = v[0], y = v[1];
        v[0] = x * mCos + y * mSin;
        v[1] = -x * mSin + y * mCos;
    }

    // q' = q * rz(a), quaternion stored as (x, y, z, w)
    void rotateQuat(float* q) const {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        q[0] = x * mHalfCos + y * mHalfSin;
        q[1] = y * mHalfCos - x * mHalfSin;
        q[2] = z * mHalfCos + w * mHalfSin;
        q[3] = w * mHalfCos - z * mHalfSin;
    }

    float mCos, mSin, mHalfCos, mHalfSin;
};

class HalProxyCallbackWrapper : public IHalProxyCallback {
  public:
    HalProxyCallbackWrapper(const sp<IHalProxyCallback>& callback, const AxisRotation& rotation)
        : mCallback(callback), mRotation(rotation) {}

    Return<void> onDynamicSensorsConnected_2_1(const hidl_vec<SensorInfo>& sensorInfos) override {
        return mCallback->onDynamicSensorsConnected_2_1(sensorInfos);
    }

    Return<void> onDynamicSensorsConnected(
            const hidl_vec<::android::hardware::sensors::V1_0::SensorInfo>& sensorInfos) override {
        return mCallback->onDynamicSensorsConnected(sensorInfos);
    }

    Return<void> onDynamicSensorsDisconnected(const hidl_vec<int32_t>& sensorHandles) override {
        return mCallback->onDynamicSensorsDisconnected(sensorHandles);
    }

    void postEvents(const std::vector<Event>& events, ScopedWakelock wakelock) override {
        std::vector<Event> rotated(events);
        for (auto& event : rotated) {
            mRotation.apply(event);
        }
        mCallback->postEvents(rotated, std::move(wakelock));
    }

    ScopedWakelock createScopedWakelock(bool lock) override {
        return mCallback->createScopedWakelock(lock);
    }

  private:
    sp<IHalProxyCallback> mCallback;
    AxisRotation mRotation;
};

class SubHalWrapper : public ISensorsSubHal {
  public:
    SubHalWrapper(ISensorsSubHal* subHal, const AxisRotation& rotation)
        : mSubHal(subHal), mRotation(rotation) {}

    Return<void> getSensorsList_2_1(getSensorsList_2_1_cb _hidl_cb) override {
        return mSubHal->getSensorsList_2_1(_hidl_cb);
    }

    Return<Result> setOperationMode(OperationMode mode) override {
        return mSubHal->setOperationMode(mode);
    }

    Return<Result> activate(int32_t sensorHandle, bool enabled) override {
        return mSubHal->activate(sensorHandle, enabled);
    }

    Return<Result> batch(int32_t sensorHandle, int64_t samplingPeriodNs,
                         int64_t maxReportLatencyNs) override {
        return mSubHal->batch(sensorHandle, samplingPeriodNs, maxReportLatencyNs);
    }

    Return<Result> flush(int32_t sensorHandle) override { return mSubHal->flush(sensorHandle); }

    Return<Result> injectSensorData_2_1(const Event& event) override {
        Event rotated(event);
        mRotation.applyInverse(rotated);
        return mSubHal->injectSensorData_2_1(rotated);
    }

    Return<void> registerDirectChannel(const SharedMemInfo& mem,
                                       registerDirectChannel_cb _hidl_cb) override {
        return mSubHal->registerDirectChannel(mem, _hidl_cb);
    }

    Return<Result> unregisterDirectChannel(int32_t channelHandle) override {
        return mSubHal->unregisterDirectChannel(channelHandle);
    }

    Return<void> configDirectReport(int32_t sensorHandle, int32_t channelHandle, RateLevel rate,
                                    configDirectReport_cb _hidl_cb) override {
        return mSubHal->configDirectReport(sensorHandle, channelHandle, rate, _hidl_cb);
    }

    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override {
        return mSubHal->debug(fd, args);
    }

    const std::string getName() override { return mSubHal->getName(); }

    Return<Result> initialize(const sp<IHalProxyCallback>& halProxyCallback) override {
        return mSubHal->initialize(new HalProxyCallbackWrapper(halProxyCallback, mRotation));
    }

  private:
    ISensorsSubHal* mSubHal;
    AxisRotation mRotation;
};

int getRotationDegrees() {
    const std::string orientation =
            android::base::GetProperty("ro.surface_flinger.primary_display_orientation", "");
    if (orientation == "ORIENTATION_90") return 90;
    if (orientation == "ORIENTATION_180") return 180;
    if (orientation == "ORIENTATION_270") return 270;
    return 0;
}

}  // namespace

extern "C" ISensorsSubHal* sensorsHalGetSubHal_2_1(uint32_t* version) {
    using GetSubHalFn = ISensorsSubHal* (*)(uint32_t*);

    void* handle = nullptr;
    const char* name = nullptr;
    for (const char* subHal : kRealSubHals) {
        handle = dlopen(subHal, RTLD_NOW);
        if (handle != nullptr) {
            name = subHal;
            break;
        }
    }
    if (handle == nullptr) {
        LOG(ERROR) << "Failed to dlopen any sub-HAL: " << dlerror();
        return nullptr;
    }

    auto getSubHal = reinterpret_cast<GetSubHalFn>(dlsym(handle, "sensorsHalGetSubHal_2_1"));
    if (getSubHal == nullptr) {
        LOG(ERROR) << "Failed to find sensorsHalGetSubHal_2_1 in " << name;
        return nullptr;
    }

    ISensorsSubHal* subHal = getSubHal(version);
    if (subHal == nullptr) {
        return nullptr;
    }

    const int degrees = getRotationDegrees();
    if (degrees == 0) {
        return subHal;
    }

    LOG(INFO) << "Rotating sensor axes by " << degrees << " degrees";
    static SubHalWrapper wrapper(subHal, AxisRotation(degrees));
    return &wrapper;
}
