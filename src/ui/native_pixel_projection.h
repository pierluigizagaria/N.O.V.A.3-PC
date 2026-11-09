#pragma once
#include "loader/elf_image.h"
#include "ui/pixel_projection.h"
#include <memory>
#include <vector>
#include <unordered_map>
namespace nova::ui {
// Scoped to an explicitly owned GUI scene draw. World/camera state and artwork
// are never changed; the driver receives an anchored projection per group.
class NativePixelProjection {
public:
    explicit NativePixelProjection(const ElfImage&);
    ~NativePixelProjection();
    class Scope {
    public:
        Scope(const std::vector<uint32_t>& groups,float factor,
              const std::unordered_map<uint32_t,uint32_t>& shared_pivots={},
              const std::unordered_map<uint32_t,std::array<float,2>>& edge_anchors={});
        ~Scope();
        Scope(const Scope&)=delete;
        std::optional<std::array<float,2>> anchor(uint32_t group) const;
    private:
        struct State;
        std::unique_ptr<State> state_;
        friend class NativePixelProjection;
    };
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
