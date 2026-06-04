#pragma once

#include "render_scene.h"

class SourceStaticPropLoader
{
public:
    void AppendStaticProps(char const* level_name, RenderSceneCpu& io_scene) const;
};
