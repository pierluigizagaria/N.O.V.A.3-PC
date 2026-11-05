#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace nova::ui {
// Source-only substitutions. Native module sizes, frame offsets and bounds are
// deliberately absent from the writable part of this plan.
struct DenseModule { unsigned module,image; int x,y,width,height; };
std::vector<DenseModule> sprite_density_plan(uint32_t logical,uint32_t dense);
std::string sprite_family(std::string name);
}
