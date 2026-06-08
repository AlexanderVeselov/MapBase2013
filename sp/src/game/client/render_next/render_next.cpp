#include "render_next.h"

#ifdef SOURCE_SDK_RENDER_NEXT

#include "render_scene.h"
#include "gpu_scene_resources.h"
#include "render_backend.h"
#include "source_adapter/source_adapter.h"
#include "source_adapter/source_material_manager.h"
#include "source_adapter/source_texture_manager.h"
#include "tasks/render_graph.h"
#include "tasks/sky_render_task.h"
#include "tasks/draw_scene_task.h"
#include "tasks/copy_depth_task.h"
#include "dx9_interop.h"
#include "mathlib/vmatrix.h"
#include "convar.h"

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
    void BuildScene(char const* level_name);
    void SyncSceneToGpu();
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
    RenderScene scene_;
    SourceTextureManager texture_manager_;
    SourceMaterialManager material_manager_;
    uint32_t bound_texture_count_ = 0;
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
    InitializeRenderBackend(__FILE__, backend_, backend_resources_);
    EnsureRenderCommandBuffer(backend_);
    texture_manager_.LoadTexture(backend_.device, *backend_.cmd_buffer, backend_.image_layouts, "");
    scene_.EnsureFallbackTextures(backend_.device, *backend_.cmd_buffer, backend_.image_layouts);
    scene_.transforms.Append(MakeIdentitySceneTransform());
    scene_.transforms.Sync(backend_.device, *backend_.cmd_buffer);
    scene_.bones.Sync(backend_.device, *backend_.cmd_buffer);
    scene_.instances.Sync(backend_.device, *backend_.cmd_buffer);
    scene_.vertex_colors.Sync(backend_.device, *backend_.cmd_buffer);
    scene_.materials.Sync(backend_.device, *backend_.cmd_buffer);
    sky_render_task_.Initialize(backend_.device, backend_resources_, scene_, texture_manager_);
    draw_scene_task_.Initialize(backend_.device, backend_resources_.view_proj_buffer, scene_);
    copy_depth_task_.Initialize(backend_.device, backend_resources_);
    UploadSkyboxTextures();
    SubmitRenderCommandsAndWait(backend_);
    draw_scene_task_.UpdateSceneBindings(backend_resources_.view_proj_buffer, scene_, texture_manager_);
    sky_render_task_.UpdateBindings(backend_resources_, scene_, texture_manager_);
    bound_texture_count_ = texture_manager_.GetTextureCount();
    render_graph_.Reset();
    render_graph_.AddTask(sky_render_task_);
    render_graph_.AddTask(draw_scene_task_);
    render_graph_.AddTask(copy_depth_task_);
}

void RenderImpl::LoadLevel(char const* level_name)
{
    BuildScene(level_name);
    EnsureRenderCommandBuffer(backend_);
    SyncSceneToGpu();
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
    draw_scene_task_.UpdateSceneBindings(backend_resources_.view_proj_buffer, scene_, texture_manager_);

    if (result.success)
    {
        ConMsg("reloadpipelines: reloaded %u pipeline(s)\n", result.reloaded_count);
    }
    else
    {
        Warning("reloadpipelines: reloaded %u pipeline(s) with errors:\n%s\n", result.reloaded_count, result.error.c_str());
    }
}

void RenderImpl::BuildScene(char const* level_name)
{
    EnsureRenderCommandBuffer(backend_);
    backend_.image_layouts.clear();
    texture_manager_.Reset();
    material_manager_.Reset();
    scene_.Reset();
    bound_texture_count_ = 0;
    engine_adapter_.BuildWorldScene(level_name, scene_, backend_.device, *backend_.cmd_buffer, backend_.image_layouts,
        texture_manager_, material_manager_);
}

void RenderImpl::SyncSceneToGpu()
{
    EnsureRenderCommandBuffer(backend_);
    SyncRenderSceneToGpu(backend_.device, *backend_.cmd_buffer, backend_.image_layouts,
        scene_, texture_manager_, material_manager_);
    UploadSkyboxTextures();
    draw_scene_task_.UpdateSceneBindings(backend_resources_.view_proj_buffer, scene_, texture_manager_);
    sky_render_task_.UpdateBindings(backend_resources_, scene_, texture_manager_);
    bound_texture_count_ = texture_manager_.GetTextureCount();
    SubmitRenderCommandsAndWait(backend_);
}

void RenderImpl::UploadSkyboxTextures()
{
    std::array<std::string, 6> skybox_texture_names;
    engine_adapter_.GetSkyboxTextureNames(skybox_texture_names);
    UploadSkyboxTexturesToGpu(backend_.device, *backend_.cmd_buffer, backend_.image_layouts, skybox_texture_names, texture_manager_, scene_);
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

    UploadBufferData(backend_.device, *backend_.cmd_buffer, backend_resources_.view_proj_staging_buffer,
        backend_resources_.view_proj_buffer, view_projection_matrix.Base(), sizeof(VMatrix));
    UploadBufferData(backend_.device, *backend_.cmd_buffer, backend_resources_.inverse_view_proj_staging_buffer,
        backend_resources_.inverse_view_proj_buffer, inverse_view_projection_matrix.Base(), sizeof(VMatrix));

    TransitionRenderImage(backend_, backend_resources_.depth_texture, gpu::ImageLayout::kRenderTarget);
}

void RenderImpl::DrawScene()
{
    RenderTaskContext task_context{backend_, backend_resources_, scene_, viewport_width_, viewport_height_};
    render_graph_.Execute(task_context);
}

void RenderImpl::FinalizeFrame()
{
    SubmitRenderCommandsAndWait(backend_);
    DX9_RenderFrame();
}

void RenderImpl::UpdateRenderableEntities()
{
    if (!backend_.device)
    {
        return;
    }

    EnsureRenderCommandBuffer(backend_);
    uint32_t texture_count_before_update = texture_manager_.GetTextureCount();
    engine_adapter_.UpdateRenderableEntities(scene_, backend_.device, *backend_.cmd_buffer,
        backend_.image_layouts, texture_manager_, material_manager_);
    scene_.transforms.Sync(backend_.device, *backend_.cmd_buffer);
    scene_.bones.Sync(backend_.device, *backend_.cmd_buffer);
    scene_.instances.Sync(backend_.device, *backend_.cmd_buffer);
    scene_.materials.Sync(backend_.device, *backend_.cmd_buffer);
    scene_.vertex_colors.Sync(backend_.device, *backend_.cmd_buffer);
    scene_.vertices.Sync(backend_.device, *backend_.cmd_buffer);
    scene_.indices.Sync(backend_.device, *backend_.cmd_buffer);
    uint32_t texture_count_after_update = texture_manager_.GetTextureCount();
    if (texture_count_after_update != texture_count_before_update || texture_count_after_update != bound_texture_count_)
    {
        draw_scene_task_.UpdateSceneBindings(backend_resources_.view_proj_buffer, scene_, texture_manager_);
        sky_render_task_.UpdateBindings(backend_resources_, scene_, texture_manager_);
        bound_texture_count_ = texture_count_after_update;
    }
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

