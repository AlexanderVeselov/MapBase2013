#include "render_next.h"

#ifdef SOURCE_SDK_RENDER_NEXT

#include "render_scene.h"
#include "gpu_scene_resources.h"
#include "render_backend.h"
#include "source_adapter.h"
#include "tasks/render_graph.h"
#include "tasks/sky_render_task.h"
#include "tasks/draw_scene_task.h"
#include "tasks/copy_depth_task.h"
#include "dx9_interop.h"
#include "mathlib/vmatrix.h"
#include "convar.h"

#include <cstring>
#include <array>
#include <string>

class RenderImpl : public RenderNext
{
public:
    void Init() override;
    void LoadLevel(char const* level_name) override;
    void RenderView(ViewSetup const& view_setup) override;
    void ReloadPipelines() override;

private:
    void BuildCpuScene(char const* level_name);
    void UploadSceneToGpu();
    void UploadSkyboxTextures();
    void PrepareFrame(ViewSetup const& view_setup);
    void DrawScene();
    void FinalizeFrame();
    void UpdateRenderableEntities();

private:
    SourceAdapter engine_adapter_;
    RenderBackendContext backend_;
    RenderBackendResources backend_resources_;
    RenderGraph render_graph_;
    SkyRenderTask sky_render_task_;
    DrawSceneTask draw_scene_task_;
    CopyDepthTask copy_depth_task_;
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
    EnsureRenderCommandBuffer(backend_);
    gpu_scene_.EnsureFallbackTextures(backend_.device, *backend_.cmd_buffer, backend_.image_layouts);
    gpu_scene_.EnsureFallbackSceneBuffers(backend_.device);
    sky_render_task_.Initialize(backend_.device, backend_resources_, gpu_scene_);
    draw_scene_task_.Initialize(backend_.device, backend_resources_.view_proj_buffer, gpu_scene_);
    copy_depth_task_.Initialize(backend_.device, backend_resources_);
    UploadSkyboxTextures();
    SubmitRenderCommandsAndWait(backend_);
    draw_scene_task_.UpdateSceneBindings(backend_resources_.view_proj_buffer, gpu_scene_);
    sky_render_task_.UpdateBindings(backend_resources_, gpu_scene_);
    render_graph_.Reset();
    render_graph_.AddTask(sky_render_task_);
    render_graph_.AddTask(draw_scene_task_);
    render_graph_.AddTask(copy_depth_task_);
}

void RenderImpl::LoadLevel(char const* level_name)
{
    BuildCpuScene(level_name);
    EnsureRenderCommandBuffer(backend_);
    UploadSceneToGpu();
}

void RenderImpl::RenderView(ViewSetup const& view_setup)
{
    UpdateRenderableEntities();
    PrepareFrame(view_setup);
    DrawScene();
    FinalizeFrame();
}

void RenderImpl::ReloadPipelines()
{
    if (!backend_.device)
    {
        Warning("reloadpipelines: render backend is not initialized\n");
        return;
    }

    gpu::PipelineReloadResult result = backend_.device->ReloadPipelines();
    draw_scene_task_.UpdateSceneBindings(backend_resources_.view_proj_buffer, gpu_scene_);

    if (result.success)
    {
        ConMsg("reloadpipelines: reloaded %u pipeline(s)\n", result.reloaded_count);
    }
    else
    {
        Warning("reloadpipelines: reloaded %u pipeline(s) with errors:\n%s\n", result.reloaded_count, result.error.c_str());
    }
}

void RenderImpl::BuildCpuScene(char const* level_name)
{
    engine_adapter_.BuildWorldScene(level_name, scene_);
}

void RenderImpl::UploadSceneToGpu()
{
    EnsureRenderCommandBuffer(backend_);
    UploadRenderSceneToGpu(backend_.device, *backend_.cmd_buffer, backend_.image_layouts, scene_, gpu_scene_);
    UploadSkyboxTextures();
    draw_scene_task_.UpdateSceneBindings(backend_resources_.view_proj_buffer, gpu_scene_);
    sky_render_task_.UpdateBindings(backend_resources_, gpu_scene_);
    SubmitRenderCommandsAndWait(backend_);
}

void RenderImpl::UploadSkyboxTextures()
{
    std::array<std::string, 6> skybox_texture_names;
    engine_adapter_.GetSkyboxTextureNames(skybox_texture_names);
    UploadSkyboxTexturesToGpu(backend_.device, *backend_.cmd_buffer, backend_.image_layouts, skybox_texture_names, gpu_scene_);
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
    VMatrix inverse_view_projection_matrix;
    MatrixInverseGeneral(view_projection_matrix, inverse_view_projection_matrix);
    MatrixTranspose(view_projection_matrix, view_projection_matrix);
    MatrixTranspose(inverse_view_projection_matrix, inverse_view_projection_matrix);

    void* mapped_data = backend_resources_.view_proj_buffer->Map();
    std::memcpy(mapped_data, view_projection_matrix.Base(), sizeof(VMatrix));
    backend_resources_.view_proj_buffer->Unmap();
    void* inverse_mapped_data = backend_resources_.inverse_view_proj_buffer->Map();
    std::memcpy(inverse_mapped_data, inverse_view_projection_matrix.Base(), sizeof(VMatrix));
    backend_resources_.inverse_view_proj_buffer->Unmap();

    TransitionRenderImage(backend_, backend_resources_.depth_texture, gpu::ImageLayout::kRenderTarget);
}

void RenderImpl::DrawScene()
{
    RenderTaskContext task_context{backend_, backend_resources_, gpu_scene_, viewport_width_, viewport_height_};
    render_graph_.Execute(task_context);
}

void RenderImpl::FinalizeFrame()
{
    SubmitRenderCommandsAndWait(backend_);
    DX9_RenderFrame();
}

void RenderImpl::UpdateRenderableEntities()
{
    engine_adapter_.UpdateRenderableEntities(scene_);

    if (!backend_.device)
    {
        return;
    }

    if (!scene_.transforms.empty())
    {
        uint64_t required_transform_buffer_size = static_cast<uint64_t>(sizeof(SceneTransform)) * scene_.transforms.size();
        if (!gpu_scene_.scene_transform_buffer || gpu_scene_.scene_transform_buffer->GetSize() < required_transform_buffer_size)
        {
            gpu_scene_.scene_transform_buffer = backend_.device->CreateBuffer(required_transform_buffer_size, sizeof(SceneTransform),
                gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
            draw_scene_task_.UpdateSceneBindings(backend_resources_.view_proj_buffer, gpu_scene_);
            Msg("Updated scene transform buffer to size %llu bytes for %u transforms\n", required_transform_buffer_size, static_cast<uint32_t>(scene_.transforms.size()));
        }
    }

    if (!gpu_scene_.scene_transform_buffer || scene_.transforms.empty())
    {
        return;
    }

    void* transform_data = gpu_scene_.scene_transform_buffer->Map();
    std::memcpy(transform_data, scene_.transforms.data(), sizeof(SceneTransform) * scene_.transforms.size());
    gpu_scene_.scene_transform_buffer->Unmap();

    gpu_scene_.uploaded_instances = scene_.instances;
    gpu_scene_.instance_count = static_cast<uint32_t>(scene_.instances.size());

    if (!scene_.instances.empty())
    {
        uint64_t required_instance_buffer_size = static_cast<uint64_t>(sizeof(RenderInstance)) * scene_.instances.size();
        if (!gpu_scene_.scene_instance_buffer || gpu_scene_.scene_instance_buffer->GetSize() < required_instance_buffer_size)
        {
            gpu_scene_.scene_instance_buffer = backend_.device->CreateBuffer(required_instance_buffer_size, sizeof(RenderInstance),
                gpu::BufferFlags::kCpuAccess | gpu::BufferFlags::kShaderResource);
            draw_scene_task_.UpdateSceneBindings(backend_resources_.view_proj_buffer, gpu_scene_);
            Msg("Updated scene instance buffer to size %llu bytes for %u instances\n", required_instance_buffer_size, gpu_scene_.instance_count);
        }
    }

    if (!gpu_scene_.scene_instance_buffer || scene_.instances.empty())
    {
        return;
    }

    void* instance_data = gpu_scene_.scene_instance_buffer->Map();
    std::memcpy(instance_data, scene_.instances.data(), sizeof(RenderInstance) * scene_.instances.size());
    gpu_scene_.scene_instance_buffer->Unmap();
}

RenderNext* GetRenderNextInstance()
{
    static RenderImpl instance;
    return &instance;
}

CON_COMMAND_F(reloadpipelines, "Reload render_next GPU pipelines.", FCVAR_CLIENTDLL)
{
    GetRenderNextInstance()->ReloadPipelines();
}

#else

class RenderNull : public RenderNext
{
public:
    void Init() override {}
    void LoadLevel(char const* level_name) override {}
    void RenderView(ViewSetup const& view_setup) override {}
    void ReloadPipelines() override {}
};

RenderNext* GetRenderNextInstance()
{
    static RenderNull instance;
    return &instance;
}

#endif

