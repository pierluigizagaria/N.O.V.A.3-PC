#include "camera/world_fov.h"
#include "platform/profile.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace {
using namespace nova::camera;
constexpr double pi = std::numbers::pi;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
void near(double a, double b, double tolerance, const char *message) {
    check(std::abs(a - b) <= tolerance, message);
}
float radians(double degrees) { return float(degrees * pi / 180.0); }
void config(const std::filesystem::path &directory) {
    std::filesystem::create_directories(directory);
    unsigned index = 0;
    auto parse = [&](const std::string &text) {
        const auto path = directory / ("world-fov-fixture-" + std::to_string(index++) + ".conf");
        { std::ofstream out(path, std::ios::binary); out << text; }
        std::filesystem::path obb;
        auto result = nova::profile::read_config(path, obb);
        std::ifstream in(path, std::ios::binary);
        std::string after((std::istreambuf_iterator<char>(in)), {});
        check(after == text, "read_config must not rewrite its standalone fixture");
        return result;
    };
    check(!parse("").world_fov, "absent world_fov preserves native optics");
    check(!parse("").weapon_fov && !parse("weapon_fov=native\n").weapon_fov,
          "missing/native weapon setting preserves native optics");
    for (auto value : {"60","100","104","100.25","1e2"})
        check(bool(parse(std::string("weapon_fov=")+value+"\n").weapon_fov),"weapon range parser");
    for (auto value : {"104.001","124","130"})
        check(parse(std::string("world_fov=120\nweapon_fov=")+value+"\n").weapon_fov==104.f,
              "legacy weapon preference clamps effectively to 104 without rewriting config");
    for (auto value : {"","Native","nan","inf","1e999","59.999","130.001","100x","100 1"}) {
        bool rejected=false;
        try { (void)parse(std::string("weapon_fov=")+value+"\n"); }
        catch (const std::runtime_error &e) { rejected=std::string(e.what()).find("weapon_fov")!=std::string::npos; }
        check(rejected,"strict named weapon config rejection");
    }
    for (bool world : {false,true}) for (bool weapon : {false,true}) {
        const auto c=parse(std::string("world_fov=")+(world ? "90" : "native")+
                           "\nweapon_fov="+(weapon ? "100" : "native")+"\n");
        check(bool(c.world_fov)==world && bool(c.weapon_fov)==weapon,"four independent parser combinations");
    }
    check(!parse("# unrelated settings\naudio=null\n").world_fov,
          "existing installations keep native optics");
    check(!parse("world_fov=native\n").world_fov, "explicit native mode");
    check(!parse("world_fov=90\nworld_fov=native\n").world_fov,
          "explicit native follows existing last-key semantics");
    for (auto value : {"60", "120", "90", "90.25", "9e1", " 90.5 \t"}) {
        auto parsed = parse(std::string("world_fov=") + value + "\n");
        check(parsed.world_fov && *parsed.world_fov >= 60 && *parsed.world_fov <= 120,
              "real parser accepts finite world FOV including both range edges");
    }
    for (auto value : {"", "Native", "NaN", "nan", "inf", "-inf", "1e999", "59.999",
                       "120.001", "0", "-90", "90degrees", "90 1", "90#comment", "true"}) {
        bool rejected = false;
        try { (void)parse(std::string("world_fov=") + value + "\n"); }
        catch (const std::runtime_error &error) {
            rejected = std::string(error.what()).find("world_fov") != std::string::npos;
        }
        check(rejected, "invalid explicit FOV must fail with named config error");
    }
    std::filesystem::path obb;
    check(!nova::profile::read_config(directory / "missing.conf", obb).world_fov,
          "missing config preserves native optics");
    check(!std::filesystem::exists(directory / "preferences.db") &&
          !std::filesystem::exists(directory / "preferences.lock"),
          "parser never touches profile persistence");
    std::puts("world_fov config: actual parser, native default, strict finite range and no persistence passed");
}
void optics() {
    constexpr std::array<std::pair<int, int>, 3> sizes{{{1280,720},{1280,800},{2520,1080}}};
    const double native_reference = 70.0 / reference_aspect * pi / 180.0;
    near(*horizontal_fov(native_reference, reference_aspect) * 180 / pi,
         64.92080256746303, 1e-10, "native pseudo70 is actual HFOV64.9208 at16:9");
    for (float setting : {60.f,90.f,120.f}) {
        const double expected_v = 2 * std::atan(std::tan(setting * pi / 360) / reference_aspect);
        double last_h = 0;
        // Increasing aspect order checks Hor+ separately from surface dimensions.
        for (double aspect : {16.0/10.0,16.0/9.0,21.0/9.0}) {
            auto native = pseudo_horizontal_lens(radians(70 / aspect), int(aspect * 900), 900);
            check(bool(native), "native pseudo-H producer accepted");
            const float v = *world_vertical_fov(*native, 70, setting);
            near(v, expected_v, 2e-7, "fixed vertical lens across16:10,16:9,21:9");
            const double h = *horizontal_fov(v, aspect);
            check(h > last_h, "wider aspect shows more horizontally"); last_h = h;
            // Independent pinhole consumers: projection, frustum plane and ray.
            const double yscale = 1 / std::tan(v / 2.0), xscale = yscale / aspect;
            const double x_edge = 1 / xscale;
            near(std::atan(x_edge) * 2, h, 1e-12, "ray edge agrees with actual horizontal angle");
            check(x_edge * .99 * xscale < 1 && x_edge * 1.01 * xscale > 1,
                  "projection and side-plane inclusion agree on both sides of edge");
            near((x_edge * .25) * xscale, .25, 1e-12,
                 "native world-marker projection agrees with ray and frustum");
        }
    }
    for (auto [width,height] : sizes) {
        const double aspect = double(width) / height;
        auto base = *pseudo_horizontal_lens(radians(70 / aspect), width, height);
        check(!world_vertical_fov(base,70,{}), "native/absent config requests exact native forwarding");
        const double configured_base = *world_vertical_fov(base,70,90.f);
        // Native target interpolator supplies pseudo-degrees at its original times.
        for (double native_degrees : {70.,65.,50.,30.,42.,61.,70.,90.,70.}) {
            auto frame = *pseudo_horizontal_lens(radians(native_degrees / aspect), width, height);
            const double effective = *world_vertical_fov(frame,70,90.f);
            const double native_zoom = std::tan((native_degrees/reference_aspect)*pi/360) /
                                       std::tan(native_reference/2);
            near(std::tan(effective/2)/std::tan(configured_base/2), native_zoom, 3e-7,
                 "aim/script enter, intermediate samples, reset and widening retain lens magnification");
            for (unsigned repeat=0;repeat<100;++repeat)
                near(*world_vertical_fov(frame,70,90.f),effective,0,
                     "repeated conversion never consumes an already adjusted value");
        }
        // Native Jeep affine output; reference companion uses actual native result.
        auto reference = base;
        float native_current = base.native_vertical;
        double expected_reference = base.reference_vertical;
        for (unsigned i=0;i<150;++i) {
            const float target = radians(i<80 ? 60 : 45), alpha = .016f;
            const float output = native_current + alpha*(target-native_current);
            auto advanced = affine_reference_lens(reference,native_current,output,alpha);
            check(bool(advanced), "native affine companion accepts attested baseline");
            expected_reference += alpha*(double(target)-expected_reference);
            near(advanced->reference_vertical,expected_reference,1.5e-6,
                 "mixed pseudo-H/true-V composition follows native timing within native float rounding");
            reference=*advanced; native_current=output;
        }
        auto no_op = affine_reference_lens(base,base.native_vertical,base.native_vertical,.25f);
        check(bool(no_op), "native Z==X is still a valid optical writer completion");
        near(no_op->reference_vertical,double(base.native_vertical)+.75*
             (base.reference_vertical-base.native_vertical),1e-12,
             "no-op native setter can still advance reference companion");
        // An aspect-only change never changes the ratio authenticated by an old writer.
        near(*world_vertical_fov(base,70,90.f),configured_base,0,
             "later node aspect or transient screen ratio cannot reinterpret stored source");
        check(!affine_reference_lens(base,radians(1),base.native_vertical,.5f),
              "raw baseline mismatch revokes composition");
    }
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const float inf=std::numeric_limits<float>::infinity();
    for (float value : {0.f,-1.f,nan,inf,radians(180),radians(360)}) {
        check(!pseudo_horizontal_lens(value,1280,720) && !vertical_lens(value),
              "invalid native lens forwards native");
    }
    check(!pseudo_horizontal_lens(radians(40),0,720) &&
          !pseudo_horizontal_lens(radians(40),1280,-1), "invalid producer dimensions rejected");
    const auto base=*pseudo_horizontal_lens(radians(39.375),1280,720);
    for (float value : {0.f,-1.f,nan,inf,400.f})
        check(!world_vertical_fov(base,value,90.f), "invalid native base forwards native");
    for (double value : {0.,-1.,double(nan),double(inf)})
        check(!horizontal_fov(base.native_vertical,value), "invalid viewport aspect rejected");
    check(!affine_reference_lens(base,base.native_vertical,base.native_vertical,nan),
          "nonfinite native interpolation cannot establish reference lens");
    std::puts("world FOV optics: real-axis conversion, Hor+, zoom lens ratios, mixed basis and no-op composition passed");
}
}
int main(int argc,char **argv) {
    try {
        if (argc==3 && std::string(argv[1])=="config") config(argv[2]);
        else if (argc==2 && std::string(argv[1])=="optics") optics();
        else throw std::runtime_error("Expected config <standalone-fixtures> or optics");
        return 0;
    } catch (const std::exception &error) {
        std::fprintf(stderr,"world-fov test failure: %s\n",error.what()); return 1;
    }
}
