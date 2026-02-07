#pragma once

#include "RHI/RHICommands.hpp"

namespace FeatherVK {
    struct RenderMesh {
        RHI::RHIBuffer *vertexBuffer{nullptr};
        RHI::RHIBuffer *indexBuffer{nullptr};
        uint32_t vertexCount{0};
        uint32_t indexCount{0};
        bool indexed{false};

        [[nodiscard]] bool IsValid() const {
            return vertexBuffer != nullptr && (indexed ? indexBuffer != nullptr && indexCount > 0 : vertexCount > 0);
        }
    };

    inline void SubmitRenderMeshDraw(RHI::RHICommandList &commandList, const RenderMesh &renderMesh) {
        if (!renderMesh.IsValid()) {
            return;
        }

        commandList.BindVertexBuffer(0, *renderMesh.vertexBuffer);
        if (renderMesh.indexed) {
            commandList.BindIndexBuffer(*renderMesh.indexBuffer);
            commandList.DrawIndexed(renderMesh.indexCount);
            return;
        }

        commandList.Draw(renderMesh.vertexCount);
    }
}
