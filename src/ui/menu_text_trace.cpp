#include "ui/menu_text_trace.h"
#include <cstring>

namespace nova::ui {
std::array<MenuTextTrace::Record,MenuTextTrace::capacity> MenuTextTrace::records_{};
void MenuTextTrace::configure(bool enabled) {
    std::lock_guard lock(mutex_);
    enabled_=enabled; count_=reported_=0;
}
MenuTextTrace::Scope::Scope(bool menu,bool ttf,uint32_t font,const char* text,uint32_t caller) {
    previous_=current_; current_=nullptr;
    std::lock_guard lock(mutex_);
    if(!enabled_ || !menu || count_==capacity) return;
    record_.ttf=ttf; record_.font=font; record_.caller=caller;
    if(text) for(size_t i=0;i+1<record_.text.size() && text[i];++i) {
        const auto c=static_cast<unsigned char>(text[i]);
        record_.text[i]=(c<32 || c=='"' || c=='\\')?' ':char(c);
    }
    active_=true; current_=&record_;
}
MenuTextTrace::Scope::~Scope() {
    current_=previous_;
    if(active_) append(record_);
}
void MenuTextTrace::glyph(Glyph sample) {
    if(!current_) return;
    auto& r=*current_;
    if(!r.glyphs) r.first=sample;
    ++r.glyphs;
    r.dense+=sample.selected;
    r.missing+=!sample.available;
    r.monochrome+=sample.monochrome;
}
void MenuTextTrace::rectangle(uint32_t width,uint32_t height) {
    if(!current_) return;
    if(!current_->rectangles) { current_->destination_width=width; current_->destination_height=height; }
    ++current_->rectangles;
}
void MenuTextTrace::append(const Record& r) {
    std::lock_guard lock(mutex_);
    for(unsigned i=0;i<count_;++i) {
        const auto& p=records_[i];
        if(p.text==r.text && p.caller==r.caller && p.font==r.font && p.ttf==r.ttf &&
           p.first.size==r.first.size && p.first.path==r.first.path &&
           p.first.outline_26_6==r.first.outline_26_6 &&
           p.first.target_width==r.first.target_width && p.first.target_height==r.first.target_height &&
           p.first.target_density==r.first.target_density && p.glyphs==r.glyphs &&
           p.first.low_width==r.first.low_width && p.first.low_height==r.first.low_height &&
           p.first.high_width==r.first.high_width && p.first.high_height==r.first.high_height &&
           p.destination_width==r.destination_width && p.destination_height==r.destination_height &&
           p.dense==r.dense && p.missing==r.missing && p.monochrome==r.monochrome && p.rectangles==r.rectangles) return;
    }
    if(count_<capacity) records_[count_++]=r;
}
void MenuTextTrace::report(FILE* output) {
    if(!output) return;
    std::lock_guard lock(mutex_);
    if(reported_==count_) return;
    while(reported_<count_) {
        const auto& r=records_[reported_++];
        const auto& g=r.first;
        std::fprintf(output,"menu-text-trace backend=%s caller=%08x font=%08x size=%u path=%u glyphs=%u dense=%u missing=%u mono=%u dense_quads=%u outline_26_6=%u first_source=%ux%u first_dense=%ux%u first_dense_destination=%ux%u target=%ux%u target_density=%u text=\"%s\"\n",
            r.ttf?"ttf":"bitmap",r.caller,r.font,g.size,g.path,r.glyphs,r.dense,r.missing,r.monochrome,r.rectangles,
            g.outline_26_6,g.low_width,g.low_height,g.high_width,g.high_height,r.destination_width,r.destination_height,g.target_width,g.target_height,g.target_density,r.text.data());
    }
    std::fflush(output);
}
}
