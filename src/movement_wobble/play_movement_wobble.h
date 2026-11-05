#pragma once
#include "native_camera_bob.h"
#include "movement_wobble/integration_gate.h"
#include <memory>
namespace nova::movement_wobble {
// Host lifecycle only. Positive gameplay eligibility remains exclusively O09.
// Explicit capability parameter is for isolated fixture injection; product uses frozen gate.
template<class Adapter> class BasicPlayMovementWobble {
    std::unique_ptr<Adapter> adapter_;
  public:
    BasicPlayMovementWobble(const ElfImage& image,const RequestedStrength& request,bool ordinary,
                           bool capability=kNativeAdapterConnected) {
        if(ordinary && capability)adapter_=std::make_unique<Adapter>(image,request);
    }
    void bind_render_thread(){if(adapter_)adapter_->bind_render_thread();}
    void prepare_step(){if(adapter_)adapter_->prepare_step();}
    void finish_step() noexcept {if(adapter_)adapter_->finish_step();}
    void shutdown() noexcept {adapter_.reset();} // Guest quiescent, after render.join().
    typename Adapter::Counters counters()const{return adapter_?adapter_->counters():typename Adapter::Counters{};}
};
using PlayMovementWobble=BasicPlayMovementWobble<NativeCameraBob>;
}
