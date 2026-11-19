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

#include <V2_0/SubHal.h>
#include <V2_1/SubHal.h>

namespace V2_0 = ::android::hardware::sensors::V2_0;
namespace V2_1 = ::android::hardware::sensors::V2_1;

using ::android::sp;
using ::android::hardware::hidl_handle;
using ::android::hardware::hidl_string;
using ::android::hardware::hidl_vec;
using ::android::hardware::Return;
using ::android::hardware::sensors::V1_0::OperationMode;
using ::android::hardware::sensors::V1_0::RateLevel;
using ::android::hardware::sensors::V1_0::Result;
using ::android::hardware::sensors::V1_0::SharedMemInfo;
using ::android::hardware::sensors::V2_0::implementation::ScopedWakelock;
using ::android::hardware::sensors::V2_1::Event;
using ::android::hardware::sensors::V2_1::ISensors;
using ::android::hardware::sensors::V2_1::SensorInfo;
using ::android::hardware::sensors::V2_1::SensorType;

namespace {

constexpr const char* kRealSubHals[] = {"sensors.qsh.so", "sensors.ssc.so"};

/*
 * Rotates sensor axes so that they match the display frame after
 * the configured display orientation is applied.
 */
class AxisRotation {
  public:
    explicit AxisRotation(int degrees) : steps_(degrees / 90) {
        const double a = degrees * M_PI / 180.0;
        cos_ = static_cast<float>(std::round(std::cos(a)));
        sin_ = static_cast<float>(-std::round(std::sin(a)));
        half_cos_ = static_cast<float>(std::cos(a / 2));
        half_sin_ = static_cast<float>(-std::sin(a / 2));
    }

    template <typename T>
    void apply(T& event) const {
        float* d = event.u.data.data();
        switch (static_cast<SensorType>(event.sensorType)) {
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
            case SensorType::DEVICE_ORIENTATION:
                rotateIndex(&d[0]);
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

    template <typename T>
    void applyInverse(T& event) const {
        AxisRotation inverse = *this;
        inverse.sin_ = -sin_;
        inverse.half_sin_ = -half_sin_;
        inverse.steps_ = -steps_;
        inverse.apply(event);
    }

  private:
    // Surface rotation index reported by DEVICE_ORIENTATION
    void rotateIndex(float* r) const {
        const int index = static_cast<int>(*r);
        *r = static_cast<float>(((index - steps_) % 4 + 4) % 4);
    }

    // v' = Rz(a) * v
    void rotateVec(float* v) const {
        const float x = v[0], y = v[1];
        v[0] = x * cos_ + y * sin_;
        v[1] = -x * sin_ + y * cos_;
    }

    // q' = q * rz(-a), quaternion stored as (x, y, z, w)
    void rotateQuat(float* q) const {
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        q[0] = x * half_cos_ + y * half_sin_;
        q[1] = y * half_cos_ - x * half_sin_;
        q[2] = z * half_cos_ + w * half_sin_;
        q[3] = w * half_cos_ - z * half_sin_;
    }

    int steps_;
    float cos_, sin_, half_cos_, half_sin_;
};

class HalProxyCallbackWrapper : public V2_1::implementation::IHalProxyCallback {
  public:
    HalProxyCallbackWrapper(const sp<V2_1::implementation::IHalProxyCallback>& callback,
                            const AxisRotation& rotation)
        : callback_(callback), rotation_(rotation) {}

    Return<void> onDynamicSensorsConnected_2_1(const hidl_vec<SensorInfo>& sensorInfos) override {
        return callback_->onDynamicSensorsConnected_2_1(sensorInfos);
    }

    Return<void> onDynamicSensorsConnected(
            const hidl_vec<::android::hardware::sensors::V1_0::SensorInfo>& sensorInfos) override {
        return callback_->onDynamicSensorsConnected(sensorInfos);
    }

    Return<void> onDynamicSensorsDisconnected(const hidl_vec<int32_t>& sensorHandles) override {
        return callback_->onDynamicSensorsDisconnected(sensorHandles);
    }

    void postEvents(const std::vector<Event>& events, ScopedWakelock wakelock) override {
        std::vector<Event> rotated(events);
        for (auto& event : rotated) {
            rotation_.apply(event);
        }
        callback_->postEvents(rotated, std::move(wakelock));
    }

    ScopedWakelock createScopedWakelock(bool lock) override {
        return callback_->createScopedWakelock(lock);
    }

  private:
    sp<V2_1::implementation::IHalProxyCallback> callback_;
    AxisRotation rotation_;
};

class SubHalWrapper : public V2_1::implementation::ISensorsSubHal {
  public:
    SubHalWrapper(V2_1::implementation::ISensorsSubHal* subHal, const AxisRotation& rotation)
        : sub_hal_(subHal), rotation_(rotation) {}

    Return<void> getSensorsList_2_1(getSensorsList_2_1_cb _hidl_cb) override {
        return sub_hal_->getSensorsList_2_1(_hidl_cb);
    }

    Return<Result> setOperationMode(OperationMode mode) override {
        return sub_hal_->setOperationMode(mode);
    }

    Return<Result> activate(int32_t sensorHandle, bool enabled) override {
        return sub_hal_->activate(sensorHandle, enabled);
    }

    Return<Result> batch(int32_t sensorHandle, int64_t samplingPeriodNs,
                         int64_t maxReportLatencyNs) override {
        return sub_hal_->batch(sensorHandle, samplingPeriodNs, maxReportLatencyNs);
    }

    Return<Result> flush(int32_t sensorHandle) override { return sub_hal_->flush(sensorHandle); }

    Return<Result> injectSensorData_2_1(const Event& event) override {
        Event rotated(event);
        rotation_.applyInverse(rotated);
        return sub_hal_->injectSensorData_2_1(rotated);
    }

    Return<void> registerDirectChannel(const SharedMemInfo& mem,
                                       registerDirectChannel_cb _hidl_cb) override {
        return sub_hal_->registerDirectChannel(mem, _hidl_cb);
    }

    Return<Result> unregisterDirectChannel(int32_t channelHandle) override {
        return sub_hal_->unregisterDirectChannel(channelHandle);
    }

    Return<void> configDirectReport(int32_t sensorHandle, int32_t channelHandle, RateLevel rate,
                                    configDirectReport_cb _hidl_cb) override {
        return sub_hal_->configDirectReport(sensorHandle, channelHandle, rate, _hidl_cb);
    }

    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override {
        return sub_hal_->debug(fd, args);
    }

    const std::string getName() override { return sub_hal_->getName(); }

    Return<Result> initialize(
            const sp<V2_1::implementation::IHalProxyCallback>& halProxyCallback) override {
        return sub_hal_->initialize(new HalProxyCallbackWrapper(halProxyCallback, rotation_));
    }

  private:
    V2_1::implementation::ISensorsSubHal* sub_hal_;
    AxisRotation rotation_;
};

class HalProxyCallbackWrapper_2_0 : public V2_0::implementation::IHalProxyCallback {
  public:
    HalProxyCallbackWrapper_2_0(const sp<V2_0::implementation::IHalProxyCallback>& callback,
                                const AxisRotation& rotation)
        : callback_(callback), rotation_(rotation) {}

    Return<void> onDynamicSensorsConnected(
            const hidl_vec<::android::hardware::sensors::V1_0::SensorInfo>& sensorInfos) override {
        return callback_->onDynamicSensorsConnected(sensorInfos);
    }

    Return<void> onDynamicSensorsDisconnected(const hidl_vec<int32_t>& sensorHandles) override {
        return callback_->onDynamicSensorsDisconnected(sensorHandles);
    }

    void postEvents(const std::vector<::android::hardware::sensors::V1_0::Event>& events,
                    ScopedWakelock wakelock) override {
        std::vector<::android::hardware::sensors::V1_0::Event> rotated(events);
        for (auto& event : rotated) {
            rotation_.apply(event);
        }
        callback_->postEvents(rotated, std::move(wakelock));
    }

    ScopedWakelock createScopedWakelock(bool lock) override {
        return callback_->createScopedWakelock(lock);
    }

  private:
    sp<V2_0::implementation::IHalProxyCallback> callback_;
    AxisRotation rotation_;
};

class SubHalWrapper_2_0 : public V2_0::implementation::ISensorsSubHal {
  public:
    SubHalWrapper_2_0(V2_0::implementation::ISensorsSubHal* subHal, const AxisRotation& rotation)
        : sub_hal_(subHal), rotation_(rotation) {}

    Return<void> getSensorsList(getSensorsList_cb _hidl_cb) override {
        return sub_hal_->getSensorsList(_hidl_cb);
    }

    Return<Result> setOperationMode(OperationMode mode) override {
        return sub_hal_->setOperationMode(mode);
    }

    Return<Result> activate(int32_t sensorHandle, bool enabled) override {
        return sub_hal_->activate(sensorHandle, enabled);
    }

    Return<Result> batch(int32_t sensorHandle, int64_t samplingPeriodNs,
                         int64_t maxReportLatencyNs) override {
        return sub_hal_->batch(sensorHandle, samplingPeriodNs, maxReportLatencyNs);
    }

    Return<Result> flush(int32_t sensorHandle) override { return sub_hal_->flush(sensorHandle); }

    Return<Result> injectSensorData(
            const ::android::hardware::sensors::V1_0::Event& event) override {
        ::android::hardware::sensors::V1_0::Event rotated(event);
        rotation_.applyInverse(rotated);
        return sub_hal_->injectSensorData(rotated);
    }

    Return<void> registerDirectChannel(const SharedMemInfo& mem,
                                       registerDirectChannel_cb _hidl_cb) override {
        return sub_hal_->registerDirectChannel(mem, _hidl_cb);
    }

    Return<Result> unregisterDirectChannel(int32_t channelHandle) override {
        return sub_hal_->unregisterDirectChannel(channelHandle);
    }

    Return<void> configDirectReport(int32_t sensorHandle, int32_t channelHandle, RateLevel rate,
                                    configDirectReport_cb _hidl_cb) override {
        return sub_hal_->configDirectReport(sensorHandle, channelHandle, rate, _hidl_cb);
    }

    Return<void> debug(const hidl_handle& fd, const hidl_vec<hidl_string>& args) override {
        return sub_hal_->debug(fd, args);
    }

    const std::string getName() override { return sub_hal_->getName(); }

    Return<Result> initialize(
            const sp<V2_0::implementation::IHalProxyCallback>& halProxyCallback) override {
        return sub_hal_->initialize(new HalProxyCallbackWrapper_2_0(halProxyCallback, rotation_));
    }

  private:
    V2_0::implementation::ISensorsSubHal* sub_hal_;
    AxisRotation rotation_;
};

int getRotationDegrees() {
    const std::string orientation = android::base::GetProperty(
            "ro.vendor.sensors.orientation",
            android::base::GetProperty("ro.surface_flinger.primary_display_orientation", ""));
    if (orientation == "ORIENTATION_90") return 90;
    if (orientation == "ORIENTATION_180") return 180;
    if (orientation == "ORIENTATION_270") return 270;
    return 0;
}

template <typename SubHal, typename Wrapper>
SubHal* getSubHal(const char* symbol, uint32_t* version) {
    using GetSubHalFn = SubHal* (*)(uint32_t*);

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

    auto getSubHalFn = reinterpret_cast<GetSubHalFn>(dlsym(handle, symbol));
    if (getSubHalFn == nullptr) {
        LOG(ERROR) << "Failed to find " << symbol << " in " << name;
        return nullptr;
    }

    SubHal* subHal = getSubHalFn(version);
    if (subHal == nullptr) {
        return nullptr;
    }

    const int degrees = getRotationDegrees();
    if (degrees == 0) {
        return subHal;
    }

    LOG(INFO) << "Rotating sensor axes by " << degrees << " degrees";
    static Wrapper wrapper(subHal, AxisRotation(degrees));
    return &wrapper;
}

}  // namespace

#ifdef SUB_HAL_2_0
extern "C" V2_0::implementation::ISensorsSubHal* sensorsHalGetSubHal(uint32_t* version) {
    return getSubHal<V2_0::implementation::ISensorsSubHal, SubHalWrapper_2_0>("sensorsHalGetSubHal",
                                                                              version);
}
#else
extern "C" V2_1::implementation::ISensorsSubHal* sensorsHalGetSubHal_2_1(uint32_t* version) {
    return getSubHal<V2_1::implementation::ISensorsSubHal, SubHalWrapper>("sensorsHalGetSubHal_2_1",
                                                                          version);
}
#endif
