#pragma once

class RenderNew
{
public:
    RenderNew() = default;
    virtual void Init() = 0;
    virtual void RenderFrame() = 0;
};

RenderNew* GetRenderNewInstance();
