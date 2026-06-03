#include "render_new.h"

#ifdef SOURCE_SDK_RENDER_NEW

#include "render_scene.h"
#include "scene_builder.h"
#include "gpu_scene_resources.h"
#include "render_backend.h"
#include "dx9_interop.h"
#include "mathlib/vmatrix.h"
#include "cliententitylist.h"
#include "icliententity.h"

#include <cstring>

class RenderImpl : public RenderNew
{
public:
    void Init() override;
    void LoadLevel(char const* level_name) override;
    void RenderView(ViewSetup const& view_setup) override;

private:
    void BuildCpuScene(char const* level_name);
    void UploadSceneToGpu();
    void PrepareFrame(ViewSetup const& view_setup);
    void DrawScene();
    void FinalizeFrame();
    void UpdateDynamicSceneTransforms();
    void TryInitializeBrushEntities();

private:
    RenderBackendContext backend_;
    RenderBackendResources backend_resources_;
    RenderSceneCpu scene_;
    RenderSceneGpu gpu_scene_;
    uint32_t viewport_width_ = 0;
    uint32_t viewport_height_ = 0;
};

void ComputeViewMatrix(VMatrix* pViewMatrix, const Vector& origin, const QAngle& angles)
{
    static VMatrix baseRotation;
    static bool bDidInit;

    if (!bDidInit)
    {
        MatrixBuildRotationAboutAxis(baseRotation, Vector(1, 0, 0), -90);
        MatrixRotate(baseRotation, Vector(0, 0, 1), 90);
        bDidInit = true;
    }

    *pViewMatrix = baseRotation;
    MatrixRotate(*pViewMatrix, Vector(1, 0, 0), -angles[2]);
    MatrixRotate(*pViewMatrix, Vector(0, 1, 0), -angles[0]);
    MatrixRotate(*pViewMatrix, Vector(0, 0, 1), -angles[1]);

    MatrixTranslate(*pViewMatrix, -origin);
}

// Taken from gl_rmain.cpp: 543
void ComputeViewMatrices(ViewSetup const& view_setup, VMatrix* pWorldToView, VMatrix* pViewToProjection, VMatrix* pWorldToProjection)
{
    ComputeViewMatrix(pWorldToView, view_setup.origin, view_setup.angles);
    MatrixBuildPerspectiveX(*pViewToProjection, view_setup.fov, view_setup.m_flAspectRatio,
        view_setup.zNear, view_setup.zFar);
    MatrixMultiply(*pViewToProjection, *pWorldToView, *pWorldToProjection);
}

void RenderImpl::Init()
{
    InitializeRenderBackend(__FILE__, backend_, backend_resources_, gpu_scene_);
}

void RenderImpl::LoadLevel(char const* level_name)
{
    BuildCpuScene(level_name);
    UploadSceneToGpu();
}

void RenderImpl::RenderView(ViewSetup const& view_setup)
{
    TryInitializeBrushEntities();
    PrepareFrame(view_setup);
    DrawScene();
    FinalizeFrame();
}

void RenderImpl::BuildCpuScene(char const* level_name)
{
    BuildRenderSceneCpu(level_name, scene_);
}

void RenderImpl::UploadSceneToGpu()
{
    EnsureRenderCommandBuffer(backend_);
    UploadRenderSceneToGpu(backend_.device, *backend_.cmd_buffer, backend_.image_layouts, scene_, backend_resources_.view_proj_buffer,
        backend_resources_.texture_sampler, backend_resources_.lightmap_sampler, backend_resources_.fallback_texture,
        backend_resources_.fallback_lightmap_texture, backend_resources_.pipeline_descriptor_set, gpu_scene_);
    SubmitRenderCommandsAndWait(backend_);
}

void RenderImpl::PrepareFrame(ViewSetup const& view_setup)
{
    viewport_width_ = backend_resources_.color_texture->GetWidth();
    viewport_height_ = backend_resources_.color_texture->GetHeight();
    EnsureRenderCommandBuffer(backend_);
    backend_.cmd_buffer->SetViewport(gpu::Viewport{0.0f, 0.0f, static_cast<float>(viewport_width_),
        static_cast<float>(viewport_height_), 0.0f, 1.0f});
    backend_.cmd_buffer->SetScissor(gpu::Rect{0, 0, static_cast<int32_t>(viewport_width_), static_cast<int32_t>(viewport_height_)});

    VMatrix view_matrix, projection_matrix, view_projection_matrix;
    ComputeViewMatrices(view_setup, &view_matrix, &projection_matrix, &view_projection_matrix);
    MatrixTranspose(view_projection_matrix, view_projection_matrix);

    UpdateDynamicSceneTransforms();

    void* mapped_data = backend_resources_.view_proj_buffer->Map();
    std::memcpy(mapped_data, view_projection_matrix.Base(), sizeof(VMatrix));
    backend_resources_.view_proj_buffer->Unmap();

    TransitionRenderImage(backend_, backend_resources_.color_texture, gpu::ImageLayout::kRenderTarget);
    TransitionRenderImage(backend_, backend_resources_.depth_texture, gpu::ImageLayout::kRenderTarget);
    backend_.cmd_buffer->SetRenderTarget(backend_resources_.color_texture, backend_resources_.depth_texture);
}

void RenderImpl::DrawScene()
{
    backend_.cmd_buffer->ClearImage(backend_resources_.color_texture, 0.0f, 0.5f, 0.5f, 1.0f);
    backend_.cmd_buffer->ClearDepthImage(backend_resources_.depth_texture, 1.0f);
    backend_.cmd_buffer->BindPipeline(backend_resources_.pipeline);
    backend_.cmd_buffer->BindDescriptorSet(backend_resources_.pipeline_descriptor_set);
    backend_.cmd_buffer->SetVertexBuffer(gpu_scene_.vertex_buffer, sizeof(Vertex));
    backend_.cmd_buffer->Draw(gpu_scene_.vertex_count);

    TransitionRenderImage(backend_, backend_resources_.depth_texture, gpu::ImageLayout::kShaderRead);
    TransitionRenderImage(backend_, backend_resources_.shared_depth_texture, gpu::ImageLayout::kShaderReadWrite);
    backend_.cmd_buffer->BindPipeline(backend_resources_.copy_depth_pipeline);
    backend_.cmd_buffer->BindDescriptorSet(backend_resources_.copy_depth_descriptor_set);
    backend_.cmd_buffer->Dispatch((viewport_width_ + 15) / 16, (viewport_height_ + 15) / 16, 1);
    backend_.cmd_buffer->StorageBarrier(backend_resources_.shared_depth_texture);
}

void RenderImpl::FinalizeFrame()
{
    SubmitRenderCommandsAndWait(backend_);
    DX9_RenderFrame();
}

void RenderImpl::UpdateDynamicSceneTransforms()
{
    if (!gpu_scene_.scene_transform_buffer || scene_.transforms.empty() || scene_.brush_entities.empty() || !cl_entitylist)
    {
        return;
    }

    for (BrushEntityInstance const& brush_entity : scene_.brush_entities)
    {
        if (brush_entity.transform_index >= scene_.transforms.size())
        {
            continue;
        }

        IClientEntity* entity = cl_entitylist->GetClientEntity(brush_entity.entity_index);
        if (!entity)
        {
            scene_.transforms[brush_entity.transform_index] = MakeIdentitySceneTransform();
            continue;
        }

        IClientRenderable* renderable = entity->GetClientRenderable();
        if (!renderable)
        {
            scene_.transforms[brush_entity.transform_index] = MakeIdentitySceneTransform();
            continue;
        }

        matrix3x4_t model_to_world;
        AngleMatrix(entity->GetAbsAngles(), entity->GetAbsOrigin(), model_to_world);
        scene_.transforms[brush_entity.transform_index] = MakeSceneTransform(model_to_world);
    }

    void* transform_data = gpu_scene_.scene_transform_buffer->Map();
    std::memcpy(transform_data, scene_.transforms.data(), sizeof(SceneTransform) * scene_.transforms.size());
    gpu_scene_.scene_transform_buffer->Unmap();
}

void RenderImpl::TryInitializeBrushEntities()
{
    bool was_initialized = scene_.brush_entities_initialized;
    size_t previous_vertex_count = scene_.vertices.size();
    InitializeBrushEntities(scene_);
    if (scene_.brush_entities_initialized == was_initialized && scene_.vertices.size() == previous_vertex_count)
    {
        return;
    }

    UploadSceneToGpu();
}

RenderNew* GetRenderNewInstance()
{
    static RenderImpl instance;
    return &instance;
}

#else

class RenderNull : public RenderNew
{
public:
    void Init() override {}
    void LoadLevel(char const* level_name) override {}
    void RenderView(ViewSetup const& view_setup) override {}
};

RenderNew* GetRenderNewInstance()
{
    static RenderNull instance;
    return &instance;
}

#endif
