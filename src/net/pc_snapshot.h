#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <deque>
#include <optional>
#include <span>
#include <vector>

namespace nova::net::pc {
inline constexpr uint8_t version = 1, native = 1, snapshots = 2, capabilities = 3,
                         lives = 4;
inline constexpr uint32_t snapshot_v1 = 1;
inline constexpr size_t header_size = 8, record_size = 29, max_players = 12;
inline constexpr uint32_t presentation_delay_ms = 67;

inline bool newer(uint32_t value, uint32_t previous) {
    const auto distance = value - previous;
    return distance && distance < 0x80000000u;
}
inline uint32_t read32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
inline uint16_t read16(const uint8_t *p) { return uint16_t(uint16_t(p[0]) << 8 | p[1]); }
inline void write32(uint8_t *p, uint32_t value) {
    for (unsigned i=0; i<4; ++i) p[i] = uint8_t(value >> ((3-i)*8));
}
inline void write16(uint8_t *p, uint16_t value) { p[0]=uint8_t(value>>8); p[1]=uint8_t(value); }
inline std::optional<uint16_t> encode_angle(double degrees) {
    if (!std::isfinite(degrees)) return {};
    double wrapped = std::fmod(degrees, 360.0);
    if (wrapped < 0) wrapped += 360.0;
    return uint16_t(uint32_t(std::floor(wrapped*(65536.0/360.0)+0.5)) & 65535u);
}
inline double decode_angle(uint16_t value) { return double(value)*(360.0/65536.0); }

struct Snapshot {
    uint8_t slot = 0;
    uint32_t generation = 0, sequence = 0, time = 0;
    std::array<float,3> position{};
    uint16_t yaw = 0, pitch = 0;
    bool valid() const {
        return slot < max_players && generation &&
            std::all_of(position.begin(),position.end(),[](float x){return std::isfinite(x);});
    }
    bool operator==(const Snapshot&) const = default;
};

// Server-authored life declarations are independent of snapshot samples. Repeat
// the current roster before each snapshot batch so loss needs no reliable queue.
struct Life {
    uint8_t slot = 0;
    uint32_t generation = 0;
    bool operator==(const Life&) const = default;
};
inline std::optional<std::vector<Life>> decode_lives(std::span<const uint8_t> body) {
    if(body.empty() || !body[0] || body[0]>max_players || body.size()!=1+size_t(body[0])*5) return {};
    std::vector<Life> out;
    uint16_t slots=0;
    for(size_t i=0;i<body[0];++i) {
        const auto *p=body.data()+1+i*5;
        Life life{p[0],read32(p+1)};
        if(life.slot>=max_players || !life.generation || (slots&(1u<<life.slot))) return {};
        slots|=uint16_t(1u<<life.slot);out.push_back(life);
    }
    return out;
}
inline std::optional<std::vector<uint8_t>> encode_lives(std::span<const Life> records) {
    if(records.empty() || records.size()>max_players) return {};
    std::vector<uint8_t> out(1+records.size()*5);
    out[0]=uint8_t(records.size());
    for(size_t i=0;i<records.size();++i) {
        out[1+i*5]=records[i].slot;write32(out.data()+2+i*5,records[i].generation);
    }
    if(!decode_lives(out)) return {};
    return out;
}

inline std::optional<std::vector<uint8_t>> encode_snapshots(std::span<const Snapshot> records) {
    if (records.empty() || records.size()>max_players) return {};
    std::vector<uint8_t> out(1+records.size()*record_size);
    out[0]=uint8_t(records.size());
    uint16_t slots=0;
    for (size_t i=0;i<records.size();++i) {
        const auto &s=records[i];
        if (!s.valid() || (slots & (1u<<s.slot))) return {};
        slots |= uint16_t(1u<<s.slot);
        auto *p=out.data()+1+i*record_size;
        p[0]=s.slot; write32(p+1,s.generation); write32(p+5,s.sequence); write32(p+9,s.time);
        for (size_t axis=0;axis<3;++axis) write32(p+13+axis*4,std::bit_cast<uint32_t>(s.position[axis]));
        write16(p+25,s.yaw); write16(p+27,s.pitch);
    }
    return out;
}

inline std::optional<std::vector<Snapshot>> decode_snapshots(std::span<const uint8_t> body) {
    if (body.empty() || !body[0] || body[0]>max_players || body.size()!=1+size_t(body[0])*record_size) return {};
    std::vector<Snapshot> out;
    uint16_t slots=0;
    for (size_t i=0;i<body[0];++i) {
        const auto *p=body.data()+1+i*record_size;
        Snapshot s;
        s.slot=p[0]; s.generation=read32(p+1); s.sequence=read32(p+5); s.time=read32(p+9);
        for (size_t axis=0;axis<3;++axis) s.position[axis]=std::bit_cast<float>(read32(p+13+axis*4));
        s.yaw=read16(p+25); s.pitch=read16(p+27);
        if (!s.valid() || (slots & (1u<<s.slot))) return {};
        slots |= uint16_t(1u<<s.slot); out.push_back(s);
    }
    return out;
}

inline bool valid_payload(uint8_t kind,std::span<const uint8_t> body) {
    switch(kind) {
    case native: return !body.empty() && body.size()<=2048;
    case snapshots: return decode_snapshots(body).has_value();
    case capabilities: return body.size()==4 && read32(body.data())==snapshot_v1;
    case lives: return decode_lives(body).has_value();
    default: return false;
    }
}
inline std::optional<std::vector<uint8_t>> encode(uint8_t kind,std::span<const uint8_t> body) {
    if (!valid_payload(kind,body)) return {};
    std::vector<uint8_t> out(header_size+body.size());
    out[0]='N';out[1]='3';out[2]='P';out[3]='C';out[4]=version;out[5]=kind;
    write16(out.data()+6,uint16_t(body.size()));
    std::copy(body.begin(),body.end(),out.begin()+header_size);
    return out;
}
struct Envelope { uint8_t kind; std::span<const uint8_t> payload; };
inline std::optional<Envelope> decode(std::span<const uint8_t> data) {
    if (data.size()<header_size || data[0]!='N' || data[1]!='3' || data[2]!='P' || data[3]!='C' ||
        data[4]!=version || size_t(read16(data.data()+6))!=data.size()-header_size) return {};
    auto body=data.subspan(header_size);
    if (!valid_payload(data[5],body)) return {};
    return Envelope{data[5],body};
}

struct Pose { std::array<float,3> position{}; float yaw=0,pitch=0; };

// Absolute rational 30 Hz deadlines, independent of frame rate. A stall emits
// one current sample and skips missed deadlines instead of sending a backlog.
class Cadence {
public:
    bool due(uint64_t now_ns) {
        if(!started_) { started_=true;epoch_=now_ns; }
        if(now_ns<epoch_ || now_ns-epoch_<next_) return false;
        const auto elapsed=now_ns-epoch_;
        const auto second=elapsed/1000000000;
        const auto index=(elapsed%1000000000)*30/1000000000+1;
        next_=second*1000000000+(index*1000000000+29)/30;
        return true;
    }
private:
    bool started_=false;
    uint64_t epoch_=0,next_=0;
};

// One history per actor. Only an authenticated host spawn/teleport event may
// reset generation. Receiving a snapshot never changes ownership or generation.
// Keep authoritative angles separate from the interpolated visual pose.
class History {
public:
    void reset(uint8_t slot,uint32_t generation) {
        slot_=slot; generation_=generation; samples_.clear();
    }
    bool push(const Snapshot &s) {
        if (!s.valid() || s.slot!=slot_ || s.generation!=generation_) return false;
        if (!samples_.empty() && (!newer(s.sequence,samples_.back().sequence) || !newer(s.time,samples_.back().time))) return false;
        samples_.push_back(s);
        if(samples_.size()>128) samples_.pop_front();
        return true;
    }
    std::optional<Pose> sample(uint32_t server_now, uint32_t delay_ms = presentation_delay_ms) const {
        if (samples_.empty()) return {};
        const uint32_t target=server_now-delay_ms;
        const auto pose=[](const Snapshot &s){return Pose{s.position,float(decode_angle(s.yaw)),float(decode_angle(s.pitch))};};
        if (!newer(target,samples_.front().time)) return pose(samples_.front());
        for(size_t i=1;i<samples_.size();++i) {
            const auto &a=samples_[i-1], &b=samples_[i];
            if (newer(target,b.time)) continue;
            const double t=double(target-a.time)/double(b.time-a.time);
            Pose out;
            for(size_t axis=0;axis<3;++axis) out.position[axis]=float(double(a.position[axis])+(double(b.position[axis])-a.position[axis])*t);
            const auto angle=[t](uint16_t from,uint16_t to) {
                const auto difference=std::bit_cast<int16_t>(uint16_t(to-from));
                double value=std::fmod(decode_angle(from)+double(difference)*(360.0/65536.0)*t,360.0);
                if(value<0) value+=360.0;
                return float(value);
            };
            out.yaw=angle(a.yaw,b.yaw);out.pitch=angle(a.pitch,b.pitch);
            return out;
        }
        // Do not extrapolate through walls during an outage. All consumers use
        // this same held pose, including follow camera and directional animation.
        return pose(samples_.back());
    }
private:
    uint8_t slot_=255;
    uint32_t generation_=0;
    std::deque<Snapshot> samples_;
};
} // namespace nova::net::pc
