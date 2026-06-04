#include "cbase.h"
#include "render_graph.h"

void RenderGraph::Reset()
{
    tasks_.clear();
}

void RenderGraph::AddTask(RenderTask& task)
{
    tasks_.push_back(&task);
}

void RenderGraph::Execute(RenderTaskContext& context) const
{
    for (RenderTask* task : tasks_)
    {
        if (!task)
        {
            continue;
        }

        task->Execute(context);
    }
}

