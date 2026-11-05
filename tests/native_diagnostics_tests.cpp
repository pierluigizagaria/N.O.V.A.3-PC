#include "input/native_diagnostics.h"
#include "input/trace.h"
#include "abi/guest_hook.h"
#include <array>
#include <chrono>
#include <cstring>
#include <future>
#include <stdexcept>

using namespace nova::input;
namespace {
using Clock = std::chrono::steady_clock;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
template<class T> uint32_t address(T &value) { return reinterpret_cast<uint32_t>(&value); }
template<class T, class V> void put(T &buffer, size_t offset, V value) {
    std::memcpy(reinterpret_cast<char *>(&buffer)+offset,&value,sizeof(value));
}
struct Guest {
    std::array<uint32_t,0x330/4> player{};
    std::array<uint32_t,0x74/4> normal{}, actor{};
    std::array<uint32_t,0x50/4> powers{};
    std::array<uint32_t,0x164/4> weapons{};
    std::array<uint32_t,0x68/4> component{};
    std::array<uint32_t,6> inventory{};
    std::array<uint32_t,8> button{};
    std::array<uint32_t,2> scheme{};
    int magazine = 31, reserve = 97;
    uint32_t *queries, *errors;
    NativeSnapshot snapshot;
    Guest(nova::ElfImage &image) :
        queries(reinterpret_cast<uint32_t *>(image.symbol("snapshot_queries"))),
        errors(reinterpret_cast<uint32_t *>(image.symbol("snapshot_argument_errors"))),
        snapshot{image,address(player),address(normal),address(scheme),
            nova::abi::require_function(image,"snapshot_ammo"),
            nova::abi::require_function(image,"snapshot_can_use"),
            nova::abi::require_function(image,"snapshot_power_type"),0,45} {
        check(queries && errors,"synthetic counters exported");
        auto table = image.symbol("input_records"); // Synthetic ELF object for real symbolization.
        auto getter = nova::abi::require_function(image,"snapshot_control");
        for (auto slot : {0x28,0x2c,0x38,0x3c,0x40,0x44})
            *reinterpret_cast<uint32_t *>(table+slot)=getter;
        scheme={table,address(button)};
        put(button,0x1d,uint8_t{1});
        put(player,4,address(actor)); put(player,0x170,address(weapons));
        put(player,0x174,address(powers)); put(player,0x32c,uint8_t{1});
        put(normal,0x70,address(button)); put(powers,0x4c,uint32_t{2});
        put(weapons,0x148,address(inventory)); put(weapons,0x14c,address(inventory)+24);
        put(inventory,0xc,address(component));
        put(component,0x44,address(magazine)); put(component,0x38,address(reserve));
        put(actor,0x68,33.f); put(actor,0x60,12.f);
    }
    void reset() { std::memset(queries,0,4*sizeof(uint32_t)); *errors=0; }
    unsigned total() const { return queries[0]+queries[1]+queries[2]+queries[3]; }
    void expect(unsigned samples) const {
        check(queries[0]==6*samples && queries[1]==samples && queries[2]==samples &&
              queries[3]==samples && !*errors,"six aligned control accessors and three correctly parameterized power queries per snapshot");
    }
    void sample(uint64_t frame, const char *reason, FILE *output) {
        snapshot.frame=frame;
        sample_native_controls(snapshot,reason,output);
    }
};
bool look(Trace &trace, uint64_t frame) {
    FrameInput input; input.dx=1; input.dy=-1;
    bool observed=trace.look_begin(frame,input,{-1,1},60,33,12);
    trace.look_end(frame,32,13);
    return observed;
}
struct LockResult { bool ready; double milliseconds; unsigned queries; };
template<class Work> LockResult locked(FILE *output, Guest &guest, Work work) {
    guest.reset();
    _lock_file(output);
    std::promise<void> entered;
    auto started=entered.get_future();
    auto worker=std::async(std::launch::async,[&] {
        entered.set_value();
        auto begin=Clock::now();
        work();
        return std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
    });
    bool scheduled=started.wait_for(std::chrono::seconds(2)) == std::future_status::ready;
    bool ready=worker.wait_for(std::chrono::milliseconds(150)) == std::future_status::ready;
    _unlock_file(output);
    auto elapsed=worker.get(); // Counters are read after joining, never raced with guest writes.
    check(scheduled,"test worker started within bounded deadline");
    return {ready,elapsed,guest.total()};
}
}
int main(int argc, char **argv) {
    try {
        check(argc>=3,"synthetic ELF and local output paths required");
        bool measure=argc==4 && std::string(argv[3])=="--measure";
        FILE *output=nullptr, *trace_output=nullptr;
        check(fopen_s(&output,argv[2],"w+b")==0,"open native diagnostic sink");
        std::setvbuf(output,nullptr,_IONBF,0);
        check(tmpfile_s(&trace_output)==0,"open separate look trace sink");
        nova::ImportRegistry imports; nova::ElfImage image(argv[1],imports);
        Guest guest(image);

        // Same scheduler callback and actual snapshot body as NativeControls.
        // Detailed mode preserves immediate responses, periodic tails and gates.
        NativeDiagnostics details(true);
        Trace trace(true,trace_output);
        std::vector<uint64_t> samples;
        guest.reset();
        for (uint64_t frame=1; frame<=570; ++frame) {
            bool observed=(frame==120 || frame==240 || frame==360) && look(trace,frame);
            details.end_frame(frame,true,observed,[&](const char *reason) {
                samples.push_back(frame); guest.sample(frame,reason,output);
            });
        }
        std::vector<uint64_t> expected;
        for (uint64_t frame=120; frame<=540; frame+=30) expected.push_back(frame);
        check(samples==expected,"30 quiet; 120 look; 150 response; sustained renewal; tail expires at 540");
        guest.expect(15);
        auto before=guest.total();
        details.end_frame(600,false,true,[&](const char *reason) { guest.sample(600,reason,output); });
        check(guest.total()==before,"non-normal/vehicle gameplay gate suppresses diagnostic queries");
        NativeDiagnostics action(true);
        action.end_frame(17,true,true,[&](const char *reason) {
            check(std::string(reason)=="response","action retains response reason");
            guest.sample(17,reason,output);
        });
        guest.expect(16);
        std::printf("periodic-contract samples=15 queries=135 frames=120..540 step=30; quiet30 expiry570 vehicle600 action17 passed\n");

        // A fast-sink control measures the complete production snapshot, including
        // guest queries, memory reads, symbolization, formatting and real writes.
        guest.reset(); auto begin=Clock::now();
        for (uint64_t i=1; i<=100; ++i) guest.sample(i,"cost-control",output);
        double fast_ms=std::chrono::duration<double,std::milli>(Clock::now()-begin).count();
        guest.expect(100);
        std::printf("snapshot-fast samples=100 queries=900 elapsed_ms=%.3f\n",fast_ms);

        // Actual default look -> observed -> end-frame -> snapshot at the former
        // blind spot. Separate look sink isolates blocking inside the snapshot.
        NativeDiagnostics ordinary;
        Trace ordinary_trace(false,trace_output);
        auto default_lock=locked(output,guest,[&] {
            for (uint64_t frame : {30,120,150})
                ordinary.end_frame(frame,true,look(ordinary_trace,frame),[&](const char *reason) {
                    guest.sample(frame,reason,output);
                });
        });
        std::printf("snapshot-default-lock ready=%d elapsed_ms=%.3f queries=%u deadline_ms=150\n",
            default_lock.ready,default_lock.milliseconds,default_lock.queries);

        // Positive control: explicit response sampling still enters the real
        // native snapshot and blocks on that same FILE at frame150.
        NativeDiagnostics explicit_tail(true);
        explicit_tail.end_frame(120,true,look(trace,120),[&](const char *reason) { guest.sample(120,reason,output); });
        auto explicit_lock=locked(output,guest,[&] {
            explicit_tail.end_frame(150,true,false,[&](const char *reason) { guest.sample(150,reason,output); });
        });
        check(!explicit_lock.ready && explicit_lock.queries==9,"explicit response snapshot really waits for controlled sink");
        guest.expect(1);
        std::printf("snapshot-explicit-lock ready=%d elapsed_ms=%.3f queries=%u frame=150\n",
            explicit_lock.ready,explicit_lock.milliseconds,explicit_lock.queries);

        // All normal physical frames, including repeated 120 boundaries and
        // immediate action observations, skip the whole snapshot callback.
        NativeDiagnostics normal;
        Trace normal_trace(false,output);
        unsigned callbacks=0; guest.reset();
        auto offset=std::ftell(output);
        begin=Clock::now();
        for (uint64_t frame=1; frame<=1200; ++frame) {
            bool observed=look(normal_trace,frame);
            normal.end_frame(frame,true,observed || frame==17,[&](const char *reason) {
                ++callbacks; guest.sample(frame,reason,output);
            });
        }
        auto bytes=std::ftell(output)-offset;
        std::printf("normal-periodic frames=1200 snapshots=%u queries=%u bytes=%ld elapsed_ms=%.3f\n",
            callbacks,guest.total(),bytes,std::chrono::duration<double,std::milli>(Clock::now()-begin).count());
        if (!measure) {
            check(default_lock.ready && !default_lock.queries,"default periodic path blocks on optional diagnostic FILE or queries guest");
            check(!callbacks && !guest.total() && !bytes,"normal path performs no optional query, symbolization or output");
        }
        std::rewind(output);
        std::string text; char buffer[4096]; size_t size;
        while ((size=std::fread(buffer,1,sizeof(buffer),output))) text.append(buffer,size);
        check(text.find("power_ammo=17")!=std::string::npos && text.find("can_grenade=1 power_type=4")!=std::string::npos &&
              text.find("yaw=33.000 pitch=12.000")!=std::string::npos && text.find("ammo=31/97")!=std::string::npos,
              "real formatter retains synthetic query and native-memory values");
        std::fclose(trace_output); std::fclose(output);
        std::puts(measure ? "measurement complete (default-policy assertions disabled)" : "native diagnostic regression passed");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr,"native diagnostic test: %s\n",error.what()); return 1;
    }
}
