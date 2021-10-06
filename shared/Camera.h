#pragma once

#include <cassert>
#include <algorithm>

#include <glm/glm.hpp>
#include <glm/gtx/euler_angles.hpp>

class CameraPositionInterface {
public:
    virtual ~CameraPositionInterface() = default;
    virtual glm::mat4 GetViewMatrix() const = 0;
    virtual glm::vec3 GetPosition() const = 0;
};

class Camera final{
public:
    explicit Camera(CameraPositionInterface& positioner)
    : mPositioner(&positioner)
    {}
    Camera(const Camera&) = default;
    Camera& operator=(const Camera&) = default;

    glm::mat4 GetViewMatrix() const { return mPositioner->GetViewMatrix(); }
    glm::vec3 GetPosition() const { return mPositioner->GetPosition(); }

private:
    const CameraPositionInterface* mPositioner;
};

class CameraPositioner_FirstPerson final : public CameraPositionInterface {
public:
    CameraPositioner_FirstPerson() = default;
    CameraPositioner_FirstPerson(const glm::vec3& pos, const glm::vec3& target, const glm::vec3& up)
    : mCameraPosition(pos)
    , mCameraOrientation(glm::lookAt(pos, target, up))
    , mUp(up)
    {}

    void Update(double deltaSeconds, const glm::vec2& mousePos, bool mousePressed) {
        if(mousePressed) {
            const glm::vec2 delta = mousePos - mMousePos;
            const glm::quat deltaQuat = glm::quat(glm::vec3(-mMouseSpeed * delta.y, mMouseSpeed * delta.x, 0.0f));
            mCameraOrientation = deltaQuat * mCameraOrientation;
            mCameraOrientation = glm::normalize(mCameraOrientation);
            SetUpVector(mUp);
        }
        mMousePos = mousePos;

        const glm::mat4 v = glm::mat4_cast(mCameraOrientation);

        const glm::vec3 forward = -glm::vec3(v[0][2], v[1][2], v[2][2]);
        const glm::vec3 right = glm::vec3(v[0][0], v[1][0], v[2][0]);
        const glm::vec3 up = glm::cross(right, forward);

        glm::vec3 accel(0.0f);

        if(mMovement.mForward) accel += forward;
        if(mMovement.mBackward) accel -= forward;

        if(mMovement.mLeft) accel -= right;
        if(mMovement.mRight) accel += right;

        if(mMovement.mUp) accel += up;
        if(mMovement.mDown) accel -= up;

        if(mMovement.mFastSpeed) accel *= mFastCoef;

        if(accel == glm::vec3(0)) {
            // decelerate naturally according to the damping value
            mMoveSpeed -= mMoveSpeed * std::min((1.0f/mDamping) * static_cast<float>(deltaSeconds), 1.0f);
        }else {
            // acceleration
            mMoveSpeed += accel * mAcceleration * static_cast<float>(deltaSeconds);
            const float maxSpeed = mMovement.mFastSpeed ? mMaxSpeed * mFastCoef : mMaxSpeed;
            if(glm::length(mMoveSpeed) > maxSpeed) mMoveSpeed = glm::normalize(mMoveSpeed) * mMaxSpeed;
        }
        mCameraPosition += mMoveSpeed * static_cast<float>(deltaSeconds);

    }

    virtual glm::mat4 GetViewMatrix() const override {
        const glm::mat4 t = glm::translate(glm::mat4(1.0f), -mCameraPosition);
        const glm::mat4 r = glm::mat4_cast(mCameraOrientation);
        return r * t;
    }

    virtual glm::vec3 GetPosition() const override {
        return mCameraPosition;
    }

    void SetPosition(const glm::vec3& pos) {
        mCameraPosition = pos;
    }

    void ResetMousePosition(const glm::vec2& p) {
        mMousePos = p;
    }

    void SetUpVector(const glm::vec3& up) {
        const glm::mat4 view = GetViewMatrix();
        const glm::vec3 dir = -glm::vec3(view[0][2], view[1][2], view[2][2]);
        mCameraOrientation = glm::lookAt(mCameraPosition, mCameraPosition + dir, up);
    }

    inline void LookAt(const glm::vec3& pos, const glm::vec3& target, const glm::vec3& up) {
        mCameraPosition = pos;
        mCameraOrientation = glm::lookAt(pos, target, up);
    }

public:
    struct Movement {
        bool mForward = false;
        bool mBackward = false;
        bool mLeft = false;
        bool mRight = false;
        bool mUp = false;
        bool mDown = false;

        bool mFastSpeed = false;
    }mMovement;

public:
    float mMouseSpeed = 4.0f;
    float mAcceleration = 150.0f;
    float mDamping = 0.2f;
    float mMaxSpeed = 10.0f;
    float mFastCoef = 10.0f;

private:
    glm::vec2 mMousePos = glm::vec2(0);
    glm::vec3 mCameraPosition = glm::vec3(0.f, 10.f, 10.f);
    glm::quat mCameraOrientation = glm::quat(glm::vec3(0));
    glm::vec3 mMoveSpeed = glm::vec3(0.0f);
    glm::vec3 mUp = glm::vec3(0.0f, 0.0f, 1.0f);
};

class CameraPositioner_MoveTo : public CameraPositionInterface {
public:
    CameraPositioner_MoveTo(const glm::vec3& pos, const glm::vec3& angles)
    : mPositionCurrent(pos)
    , mPositionDesired(pos)
    , mAnglesCurrent(angles)
    , mAnglesDesired(angles)
    {}

    void Update(float deltaSeconds, const glm::vec2& mousePos, bool mousePressed) {
        mPositionCurrent += mDampingLinear * deltaSeconds * (mPositionDesired - mPositionCurrent);

        // normalization is required to avoid "spinning" around the object 2pi times
        mAnglesCurrent = ClipAngles(mAnglesCurrent);
        mAnglesDesired = ClipAngles(mAnglesDesired);

        // update angles
        mAnglesCurrent -= AngleDelta(mAnglesCurrent, mAnglesDesired) * mDampingEulerAngles * deltaSeconds;

        // normalize new angles
        mAnglesCurrent = ClipAngles(mAnglesCurrent);

        const glm::vec3 a = glm::radians(mAnglesCurrent);

        mCurrentTransform = glm::translate(glm::yawPitchRoll(a.y, a.x, a.z), -mPositionCurrent);
    }

    void SetPosition(const glm::vec3& p) { mPositionCurrent = p; }
    void SetAngles(float pitch, float pan, float roll) { mAnglesCurrent = glm::vec3(pitch, pan, roll); }
    void SetAngles(const glm::vec3& angles) { mAnglesCurrent = angles; }
    void SetDesiredPosition(const glm::vec3& p) { mPositionDesired = p; }
    void SetDesiredAngles(float pitch, float pan, float roll) { mAnglesDesired = glm::vec3(pitch, pan, roll); }
    void SetDesiredAngles(const glm::vec3& angles) { mAnglesCurrent = angles; }

    virtual glm::vec3 GetPosition() const override { return mPositionCurrent; }
    virtual glm::mat4 GetViewMatrix() const override { return mCurrentTransform; }

public:
    float mDampingLinear = 10.0f;
    glm::vec3 mDampingEulerAngles = glm::vec3(5.0f, 5.0f, 5.0f);

private:
    glm::vec3 mPositionCurrent = glm::vec3(0.0f);
    glm::vec3 mPositionDesired = glm::vec3(0.0f);

    // pitch, pan, roll
    glm::vec3 mAnglesCurrent = glm::vec3(0.0f);
    glm::vec3 mAnglesDesired = glm::vec3(0.0f);

    glm::mat4 mCurrentTransform = glm::mat4(1.0f);

    static inline float ClipAngle(float d) {
        if(d < -180.0f) return d + 360.0f;
        if(d > +180.0f) return d - 360.0f;
        return d;
    }

    static inline glm::vec3 ClipAngles(const glm::vec3& angles) {
        return {
            std::fmod(angles.x, 360.0f),
            std::fmod(angles.y, 360.0f),
            std::fmod(angles.z, 360.0f)
        };
    }

    static inline glm::vec3 AngleDelta(const glm::vec3& anglesCurrent, const glm::vec3& anglesDesired) {
        const glm::vec3 d = ClipAngles(anglesCurrent) - ClipAngles(anglesDesired);
        return {
                ClipAngle(d.x),
                ClipAngle(d.y),
                ClipAngle(d.z)
        };
    }
};