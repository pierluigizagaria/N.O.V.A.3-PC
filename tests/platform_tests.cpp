#include "abi/guest_call.h"
#include "java/boot.h"
#include "platform/render_settings.h"
#include "loader/diagnostics.h"
#include "net/network.h"
#include "platform/profile.h"
#include <cstdio>
#include <array>
#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    if (argc < 4)
        return 2;
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    try {
        std::filesystem::path fixture = argv[1];
        std::string mode = argv[2];
        nova::bionic::PathConfig paths;
        paths.profile_root = argv[3];
        nova::bionic::configure_paths(paths);
        nova::profile::Config config;
        config.language = "it-IT";
        config.net_log = true;
        config.hosts["community.test"] = "127.0.0.1";
        nova::profile::configure(config);
        nova::ImportRegistry imports;
        nova::bionic::register_boot(imports);
        nova::bionic::CxaRegistry cxa(imports);
        nova::ElfImage image(fixture, imports);
        nova::Diagnostics diagnostics(image);
        uint32_t result = 0;
        if (mode == "guest-extent") {
            using nova::platform::GuestExtent;
            using nova::platform::GuestExtentBlock;
            using nova::platform::startup_guest_extent;
            const auto supported=startup_guest_extent({1600,900});
            require(supported.active==GuestExtent{1600,900} && supported.block==GuestExtentBlock::none,
                    "1600 selects genuine startup render extent without changing profile");
            const auto normal=startup_guest_extent();
            require(normal.active==GuestExtent{} && normal.block==GuestExtentBlock::none,
                    "verified startup extent remains exact");
            for(auto requested : {GuestExtent{1920,1080},GuestExtent{1920,1200},
                                  GuestExtent{7680,4320},GuestExtent{1280,800}}) {
                const auto selected=startup_guest_extent(requested);
                require(selected.requested==requested && selected.active==GuestExtent{},
                        "unsupported native extent falls back as a complete pair");
                require(selected.block==(requested.width==1920 ? GuestExtentBlock::native_1920_profile
                                                              : GuestExtentBlock::native_target_lifecycle),
                        "1920 profile dependency is distinguished from unverified lifecycle");
            }
            for(const auto extent:nova::platform::standard_render_extents) {
                require(nova::platform::parse_render_extent(render_extent_name(extent))==extent,"all standard native names roundtrip exactly");
                if(extent.width!=1920) require(startup_guest_extent(extent).active==extent,"ordinary standard widths retain the native startup path");
            }
            const nova::platform::RenderLimits high{4096,4096,4096,4096},low{2048,4096,4096,4096};
            const std::array modes{GuestExtent{1920,1080},GuestExtent{2560,1440},GuestExtent{1366,768},GuestExtent{1280,1024}};
            const auto catalogue=nova::platform::render_catalogue(high,modes,{2560,1440});
            require(catalogue.size()==nova::platform::standard_render_extents.size(),"4K-capable renderer exposes the whole finite native catalogue");
            require(std::count_if(catalogue.begin(),catalogue.end(),[](const auto &entry) { return entry.recommended; })==1,
                    "desktop recommendation is unique");
            require(std::any_of(catalogue.begin(),catalogue.end(),[](const auto &entry) {
                        return entry.extent==GuestExtent{2560,1440} && entry.recommended && entry.display_mode;
                    }),"desktop mode annotates an actual render option without changing its dimensions");
            const auto limited=nova::platform::render_catalogue(low,modes,{3840,2160});
            require(limited.back().extent==GuestExtent{2048,1152} && !low.admits({2560,1440}) &&
                    !high.admits({1920,1200}) && !high.admits({1366,768}),"texture caps and unproved aspects are filtered");
            require(nova::platform::render_catalogue({4096,4096,1600,900}).back().extent==GuestExtent{1600,900},
                    "viewport caps independently constrain the catalogue");
            require(nova::platform::render_catalogue({}).empty(),"unknown capabilities never fabricate available render choices");
            for(auto invalid : {GuestExtent{0,720},GuestExtent{1280,-1}}) {
                bool rejected=false;
                try { startup_guest_extent(invalid); }
                catch(const std::invalid_argument &) { rejected=true; }
                require(rejected,"invalid native extents fail before registration");
            }
            // Host-only callback seam: these values are never passed to real
            // native startup. Non-default numbers expose accidental constants.
            for(auto extent : {GuestExtent{},GuestExtent{960,600},GuestExtent{1366,768}}) {
                nova::jni::Runtime vm(image);
                const auto expected=extent;
                nova::java::register_boot(vm,extent);
                extent={320,200}; // Registered callbacks own an immutable snapshot.
                auto cls=vm.find_class("com/gameloft/android/ANMP/GloftN3HM/GL2JNILib");
                auto width=vm.method(cls,"GetWindowWidth","()I",true);
                auto height=vm.method(cls,"GetWindowHeight","()I",true);
                require(vm.invoke(cls,width,{}).i==expected.width &&
                        vm.invoke(cls,height,{}).i==expected.height,
                        "actual Java callbacks return the supplied startup snapshot");
                vm.release(cls,1);
            }
            auto live=std::make_shared<nova::platform::GuestExtentState>();
            nova::jni::Runtime vm(image); nova::java::register_boot(vm,{},live);
            auto cls=vm.find_class("com/gameloft/android/ANMP/GloftN3HM/GL2JNILib");
            auto width=vm.method(cls,"GetWindowWidth","()I",true),height=vm.method(cls,"GetWindowHeight","()I",true);
            for(auto changed:{GuestExtent{1600,900},GuestExtent{1920,1080},GuestExtent{}}) {
                live->publish(changed);
                require(vm.invoke(cls,width,{}).i==changed.width && vm.invoke(cls,height,{}).i==changed.height,
                        "actual Java callbacks follow provisional, applied and rollback pairs");
            }
            vm.release(cls,1);
            nova::platform::RenderSettings choice(GuestExtent{});
            choice.initialize({},high);
            require(choice.select({1920,1080}) && choice.snapshot().active==GuestExtent{} && !choice.request(),
                    "staging neither applies nor bypasses unavailable backend");
            choice.available(true); require(choice.request(),"available backend accepts explicit Apply");
            auto request=choice.take();
            require(request && !choice.take() && !choice.select({1600,900}) && !choice.request(),
                    "in-flight Apply is consumed once and cannot be overwritten");
            choice.complete(*request,false,false,"native resource refusal");
            require(choice.snapshot().active==GuestExtent{} && !choice.snapshot().busy && !choice.snapshot().error.empty(),
                    "native refusal preserves active dimensions and exposes retryable error");
            require(choice.request(),"refused Apply can be retried"); request=choice.take();
            choice.complete(*request,true,false,{},1000);
            require(choice.snapshot(1000).active==GuestExtent{1920,1080} && choice.snapshot(1000).confirming &&
                    choice.snapshot(1000).seconds_remaining==15 && !choice.request() && !choice.select({1600,900}),
                    "preview requires confirmation and cannot be overwritten");
            require(!choice.take(15999) && choice.decide(true,15999),"confirmation before deadline is accepted");
            request=choice.take(15999);
            require(request && request->action==nova::platform::RenderAction::keep,"confirmation requests persistence only");
            choice.complete(*request,true,false,"save failed",15999);
            require(choice.snapshot().unsaved && !choice.snapshot().confirming,"failed save permits explicit retry");
            require(choice.request(),"same active Apply can retry failed persistence"); request=choice.take();
            choice.complete(*request,true,true,{});
            require(!choice.snapshot().unsaved && choice.snapshot().error.empty() && !choice.request(),
                    "successful persistence clears error and idle Apply never repeats");
            require(choice.select({1600,900}) && choice.request(),"second preview begins");
            request=choice.take(); choice.complete(*request,true,false,{},20000);
            require(!choice.take(34999),"preview remains until its deadline");
            request=choice.take(35000);
            require(request && request->action==nova::platform::RenderAction::revert &&
                    request->extent==GuestExtent{1920,1080},"timeout restores last confirmed dimensions");
            choice.complete(*request,false,false,"transient native boundary",35000);
            require(!choice.take(35999),"failed recovery is rate limited");
            request=choice.take(36000); require(request.has_value(),"failed recovery retries");
            choice.complete(*request,true,false,{},36000);
            require(!choice.snapshot().confirming && choice.snapshot().active==GuestExtent{1920,1080} &&
                    choice.snapshot().selected==choice.snapshot().active && !choice.snapshot().unsaved,
                    "recovery restores actual selection without saving preview");
            require(choice.select({1600,900}) && choice.request(),"expired confirmation setup");
            request=choice.take(); choice.complete(*request,true,false,{},40000);
            require(choice.decide(true,55000),"late confirmation processed");
            request=choice.take(55000);
            require(request && request->action==nova::platform::RenderAction::revert,"expired Keep cannot persist preview");
            choice.complete(*request,true,false,{},55000);
            require(choice.select({1600,900}) && choice.request(),"explicit revert setup");
            request=choice.take(); choice.complete(*request,true,false,{},60000);
            require(choice.decide(false,60001),"explicit revert accepted immediately");
            request=choice.take(60001); choice.complete(*request,true,false,{},60001);
            nova::platform::RenderSettings fallback({3840,2160}); fallback.initialize({},low);
            require(fallback.snapshot().active==GuestExtent{} && !fallback.snapshot().error.empty() &&
                    !fallback.select({3840,2160}),"unsupported saved4K falls back honestly and cannot enter Apply");
        } else if (mode == "policy-cache") {
            using nova::java::installer_host_validity;
            constexpr int64_t now = 1793736000;
            for (auto value : {"0", "-1", "1793735999", "1793736000"})
                require(installer_host_validity("LICENSED", value, now) == "1793822400",
                        "expired licensed host seed renews for one day");
            require(installer_host_validity("LICENSED", "1793822401", now) == "1793822401",
                    "future host seed is unchanged");
            for (auto response : {"NOT_LICENSED", "RETRY", "UNKNOWN", "licensed", ""})
                require(installer_host_validity(response, "0", now) == "0",
                        "nonlicensed host seed must not renew");
            for (auto malformed : {"", "0junk", " 0", "+0", "9223372036854775808"}) {
                bool rejected = false;
                try { installer_host_validity("LICENSED", malformed, now); }
                catch (const std::runtime_error &) { rejected = true; }
                require(rejected, "malformed host validity must fail explicitly");
            }
            for (auto invalid_now : {int64_t(-1), std::numeric_limits<int64_t>::max()}) {
                bool rejected = false;
                try { installer_host_validity("LICENSED", "0", invalid_now); }
                catch (const std::runtime_error &) { rejected = true; }
                require(rejected, "invalid host time must not overflow");
            }
        } else if (mode == "profile-write" || mode == "profile-read") {
            nova::jni::Runtime vm(image);
            nova::java::register_boot(vm);

            // Every name in the original inventory must have an explicit policy and registered callback.
            std::ifstream catalog(std::filesystem::path(NOVA_SOURCE_DIR) /
                                  "assets/manifests/gl2jnilib_callback_policy.tsv");
            std::set<std::string> registered;
            std::string line;
            auto cls = vm.find_class("com/gameloft/android/ANMP/GloftN3HM/GL2JNILib");
            while (std::getline(catalog, line)) {
                if (line.empty() || line[0] == '#')
                    continue;
                auto first = line.find('\t'), second = line.find('\t', first + 1);
                require(first != std::string::npos && second != std::string::npos, "policy columns");
                auto name = line.substr(0, first), signature = line.substr(first + 1, second - first - 1);
                auto method = vm.method(cls, name.c_str(), signature.c_str(), true);
                require(method && bool(method->callback), "missing explicit Java callback");
                registered.insert(name);
            }
            vm.release(cls, 1);
            std::ifstream inventory(std::filesystem::path(NOVA_SOURCE_DIR) /
                                    "assets/manifests/gl2jnilib_methods_named_in_native.txt");
            std::set<std::string> expected;
            while (std::getline(inventory, line)) {
                if (line.ends_with('\r'))
                    line.pop_back();
                if (!line.empty() && line[0] != '#')
                    expected.insert(line);
            }
            require(expected.size() == 52 && registered == expected, "callback inventory coverage");
            uint32_t args[] = {reinterpret_cast<uint32_t>(vm.env()), mode == "profile-read" ? 1u : 0u};
            result = nova::abi::call_guest(image.lookup("fixture_profile"), args, 2);
            require(nova::profile::get("Installer", "licensed") == "LICENSED",
                    "installer enum persistence");
            require(nova::profile::get("Installer", "boolean") == "false", "installer boolean persistence");
            require(nova::profile::get_number("Installer", "long", 0) == 0x123456789LL,
                    "installer long persistence");
            auto config_path = nova::bionic::profile_directory() / "test.conf";
            std::ofstream(config_path)
                << "# test\nlanguage=ja-JP\nobb_directory=obb\ngraphics_backend=vulkan\n";
            std::filesystem::path obb;
            auto parsed = nova::profile::read_config(config_path, obb);
            require(parsed.language == "ja-JP", "public language configuration");
            require(obb == config_path.parent_path() / "obb", "relative config path");
            std::filesystem::path diagnostic_obb;
            std::ofstream(config_path, std::ios::app) << "hosts.GLLIVE.GAMELOFT.COM.=127.0.0.1\n";
#if defined(NOVA_DEVELOPER_INTERFACE)
            require(nova::profile::read_config(config_path, diagnostic_obb).hosts.at("gllive.gameloft.com") == "127.0.0.1", "Debug host normalization");
#else
            bool rejected_hosts = false;
            try { nova::profile::read_config(config_path, diagnostic_obb); }
            catch (const std::exception &) { rejected_hosts = true; }
            require(rejected_hosts, "Release rejects diagnostic host overrides");
#endif
            require(parsed.graphics_backend == "vulkan", "graphics backend config");
            nova::bionic::map_path("/data/data/com.gameloft.android.ANMP.GloftN3HM/files/CaseName");
            nova::bionic::map_path("/data/data/com.gameloft.android.ANMP.GloftN3HM/files/casename");
            auto tricky = std::string("space\n\t\\");
            nova::profile::put(tricky, tricky, tricky);
            require(nova::profile::get(tricky, tricky) == tricky, "escaped storage");
            for (auto name : {"udid", "android_id", "mac", "imei", "serial", "hostname"})
                std::printf("ID %s=%s\n", name, nova::profile::identifier(name).c_str());
            std::printf("MD5=%s\n", nova::profile::mac_digest().c_str());
            for (auto [locale, want] :
                 {std::pair{"it-IT", "it"}, std::pair{"ja-JP", "jp"}, std::pair{"ko-KR", "kr"},
                  std::pair{"pt-BR", "br"}, std::pair{"pt-PT", "en"}, std::pair{"zh-CN", "zh"},
                  std::pair{"zh-TW", "en"}, std::pair{"pl-PL", "en"}})
                require(nova::profile::language_for_locale(locale) == want, "language mapping");
        } else if (mode == "server") {
            auto s = nova::abi::call0(image.lookup("fixture_server_start"));
            require(static_cast<int>(s) >= 3, "start server");
            auto port = *reinterpret_cast<uint32_t *>(image.lookup("fixture_port"));
            std::printf("READY %u\n", port);
            result = nova::abi::call_guest(image.lookup("fixture_server_exchange"), &s, 1);
            nova::profile::put("Test", "peer", "server");
        } else if (mode == "client") {
            require(argc == 5, "client port required");
            auto port = static_cast<uint32_t>(std::stoul(argv[4]));
            result = nova::abi::call_guest(image.lookup("fixture_client_exchange"), &port, 1);
            nova::profile::put("Test", "peer", "client");
        } else {
            result = nova::abi::call0(image.lookup("fixture_" + mode));
            if (mode == "errors") {
                require(nova::net::socket_error(WSAENETUNREACH) == 101, "network errno");
                require(nova::net::socket_error(WSAETIMEDOUT) == 110, "timeout errno");
                require(nova::net::socket_error(WSAECONNRESET) == 104, "reset errno");
            }
        }
        nova::bionic::stream_flush(nullptr);
        if (result == 77 && mode == "broadcast")
            return 77;
        if (result) {
            std::fprintf(stderr, "FAIL %s at platform.c:%u\n", mode.c_str(), result);
            return 1;
        }
        std::printf("PASS %s ELF32 platform fixture\n", mode.c_str());
        return 0;
    } catch (const std::exception &e) {
        std::fprintf(stderr, "platform test: %s\n", e.what());
        return 1;
    }
}
