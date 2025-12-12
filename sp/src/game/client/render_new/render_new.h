#pragma once

#include "mathlib/mathlib.h"

struct ViewSetup
{
    // horizontal FOV in degrees
    float fov;
    // horizontal FOV in degrees for in-view model
    //float fovViewmodel;

    // 3D origin of camera
    Vector origin;

    // heading of camera (pitch, yaw, roll)
    QAngle angles;
    // local Z coordinate of near plane of camera
    float zNear;
    // local Z coordinate of far plane of camera
    float zFar;

    // local Z coordinate of near plane of camera ( when rendering view model )
    //float zNearViewmodel;
    // local Z coordinate of far plane of camera ( when rendering view model )
    //float zFarViewmodel;

    // set to true if this is to draw into a subrect of the larger screen
    // this really is a hack, but no more than the rest of the way this class is used
    //bool m_bRenderToSubrectOfLargerScreen;

    // The aspect ratio to use for computing the perspective projection matrix
    // (0.0f means use the viewport)
    float m_flAspectRatio;

    // Controls for off-center projection (needed for poster rendering)
    //bool m_bOffCenter;
    //float m_flOffCenterTop;
    //float m_flOffCenterBottom;
    //float m_flOffCenterLeft;
    //float m_flOffCenterRight;

    // Control that the SFM needs to tell the engine not to do certain post-processing steps
    //bool m_bDoBloomAndToneMapping;

    // Cached mode for certain full-scene per-frame varying state such as sun entity coverage
    //bool m_bCacheFullSceneState;

    // This does NOT override the Z range - that will be set up as normal (i.e. the values in this matrix will be ignored).
    //bool m_bViewToProjectionOverride;
    //VMatrix m_ViewToProjection;
};

class RenderNew
{
public:
    RenderNew() = default;
    virtual void Init() = 0;
    virtual void RenderView(ViewSetup const& view_setup) = 0;
};

RenderNew* GetRenderNewInstance();
