#include "java/boot.h"
#include "bionic/runtime.h"
#include "platform/profile.h"
#include "net/local_shop.h"
#include "net/social_invitations.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <charconv>
#include <cstdio>
#include <ctime>
#include <map>
#include <limits>
#include <mutex>
#include <stdexcept>
namespace nova::java {
namespace {
const std::string base = "com/gameloft/android/ANMP/GloftN3HM/";
const std::string lib = base + "GL2JNILib", activity = base + "GL2JNIActivity";
const std::string symbol = "Java_com_gameloft_android_ANMP_GloftN3HM_GL2JNILib_";
constexpr auto data = "/sdcard/Android/data/com.gameloft.android.ANMP.GloftN3HM/files";
constexpr auto obb = "/sdcard/Android/obb/com.gameloft.android.ANMP.GloftN3HM";
constexpr auto home = "/data/data/com.gameloft.android.ANMP.GloftN3HM/files";
constexpr auto cache = "/data/data/com.gameloft.android.ANMP.GloftN3HM/cache";
std::string policy_space = "Installer";
std::atomic<int> game_state{1};
std::atomic<int> game_language{0};
const char *languages[] = {"en", "fr", "de", "es", "it", "jp", "kr", "zh", "br", "ru"};
bool view_requested = false;
jni::Value number(int n) {
    jni::Value v;
    v.i = n;
    return v;
}
void paths(jni::Runtime &runtime) {
    auto c = runtime.find_class(lib);
    std::vector<uint32_t> args;
    std::vector<jni::Handle> refs;
    for (auto p : {obb, data, home, cache}) {
        auto h = runtime.string(p);
        refs.push_back(h);
        args.push_back(reinterpret_cast<uint32_t>(h));
    }
    runtime.native(symbol + "setPaths", c, args);
    for (auto h : refs)
        runtime.release(h, 1);
    runtime.release(c, 1);
}
} // namespace
std::string installer_host_validity(const std::string &response, const std::string &validity,
                                    int64_t now) {
    if (response != "LICENSED")
        return validity;
    int64_t expiry = 0;
    auto parsed = std::from_chars(validity.data(), validity.data() + validity.size(), expiry);
    if (parsed.ec != std::errc{} || parsed.ptr != validity.data() + validity.size())
        throw std::runtime_error("Invalid installer host validity timestamp");
    if (now < 0 || now > std::numeric_limits<int64_t>::max() - 86400)
        throw std::runtime_error("Invalid installer host time");
    // Provisional L02: renew only the accepted synthetic LICENSED host seed.
    // Keep nativeAllow authoritative; never process or invent a license response.
    return expiry <= now ? std::to_string(now + 86400) : validity;
}
void register_boot(jni::Runtime &r, platform::GuestExtent extent,
                   std::shared_ptr<platform::GuestExtentState> mutable_extent) {
    extent.validate();
    auto integer = [&](const std::string &cls, const char *name, const char *sig, int value) {
        r.register_method(cls, name, sig, [value](auto &, auto, const auto &) { return number(value); });
    };
    auto string = [&](const std::string &cls, const char *name, const char *sig, const char *value,
                      bool is_static = true) {
        r.register_method(
            cls, name, sig,
            [text = std::string(value)](auto &vm, auto, const auto &) {
                jni::Value v;
                v.l = vm.string(text);
                return v;
            },
            is_static);
    };
    auto noop = [&](const std::string &cls, const char *name, const char *sig) {
        r.register_method(cls, name, sig, [name = std::string(name)](auto &, auto, const auto &) {
            bionic::log_once("Java callback recorded: " + name);
            return jni::Value{};
        });
    };
    noop(base + "installer/GameInstaller", "startGame", "()V");
    r.register_method(base + "installer/GDRMPolicy", "UpdatePreferences",
                      "(Ljava/lang/String;Ljava/lang/String;I)V", [](auto &vm, auto, const auto &a) {
                          auto value = vm.text(a[1].l);
                          if (a[2].i == 1)
                              value = "LICENSED";
                          else if (a[2].i == 6)
                              value = value == "0" ? "false" : "true";
                          profile::put(policy_space, vm.text(a[0].l), value);
                          return jni::Value{};
                      });
    r.register_method(base + "installer/GDRMPolicy", "UpdatePreferences2", "(Ljava/lang/String;JI)V",
                      [](auto &vm, auto, const auto &a) {
                          profile::put_number(policy_space, vm.text(a[0].l), a[1].j);
                          return jni::Value{};
                      });
    r.register_method(base + "PushNotification/C2DMAndroidUtils", "SetOfflineDeviceCredential", "()I",
        [](auto &, auto, const auto &) {
            bionic::log_once("Push credential request recorded locally; push service disabled");
            return number(0); // Original method returns 0 after scheduling its service task.
        });
    r.register_method(base + "PushNotification/C2DMAndroidUtils", "HasPushNotification", "()I",
        [](auto &, auto, const auto &) { return number(net::social::poll_invitation() ? 1 : 0); });
    r.register_method(base + "PushNotification/C2DMAndroidUtils", "ResetNotificationStatus", "()V",
        [](auto &, auto, const auto &) { net::social::reset_notification(); return jni::Value{}; });
    r.register_method(base + "PushNotification/C2DMAndroidUtils", "GetUserID", "()Ljava/lang/String;",
        [](auto &vm, auto, const auto &) { jni::Value v; v.l = vm.string(net::social::inviter()); return v; });
    r.register_method(base + "PushNotification/C2DMAndroidUtils", "GetBundleData", "()Landroid/os/Bundle;",
        [](auto &vm, auto, const auto &) {
            auto bundle = std::make_shared<jni::Object>();
            bundle->klass = vm.klass("android/os/Bundle");
            bundle->name = net::social::inviter().empty() ? "" : "nova-invitation";
            jni::Value v; v.l = vm.reference(bundle); return v;
        });
    r.register_method("android/os/Bundle", "getInt", "(Ljava/lang/String;)I",
        [](auto &vm, auto self, const auto &args) {
            const auto bundle = vm.object(self);
            return number(bundle && bundle->name == "nova-invitation" &&
                          vm.text(args[0].l) == "pn_goto_multiplayer" ? 1 : 0);
        }, false);
    r.register_method("android/os/Bundle", "containsKey", "(Ljava/lang/String;)Z",
        [](auto &vm, auto self, const auto &args) {
            const auto bundle = vm.object(self);
            const auto key = vm.text(args[0].l);
            return number(bundle && bundle->name == "nova-invitation" &&
                          (key == "pn_goto_multiplayer" || key == "pn_launch_game") ? 1 : 0);
        }, false);
    integer(activity, "IsSupportNeon", "()Z", 0);
    integer(activity, "IsSupportOpenSLES", "()I", 1);
    integer(activity, "IsForceAndroid8", "()Z", 0);
    integer(activity, "IsTegra4", "()Z", 1);
    noop(activity, "showGPUSelect", "()V"); // GPU_5 is selected by IsTegra4; no picker in the probe.
    integer(activity, "IsPowerAConnected", "()Z", 0);
    integer(activity, "keepScreenOn", "(Z)Z", 0);
    string(activity, "LibraryToLoad", "()Ljava/lang/String;", "libNOVA3_neon.so");
    string(activity, "getVersion", "()Ljava/lang/String;", "1.0.7");
    string(activity, "getSDFolder", "()Ljava/lang/String;", data);
    for (auto name : {"GetDeviceName", "GetPhoneModel"})
        string(lib, name, "()Ljava/lang/String;", "Nova3PC x86");
    string(lib, "GetDeviceFirmware", "()Ljava/lang/String;", "2.3.7");
    auto language = profile::language();
    string(lib, "GetPhoneLanguage", "()Ljava/lang/String;", language.c_str());
    for (int i = 0; i < 10; ++i)
        if (language == languages[i])
            game_language = i;
    r.register_method(lib, "getGameLang", "()Ljava/lang/String;", [](auto &vm, auto, const auto &) {
        jni::Value v;
        v.l = vm.string(languages[game_language.load()]);
        return v;
    });
    r.register_method(lib, "setGameLang", "(I)V", [](auto &, auto, const auto &a) {
        game_language = a[0].i > 0 && a[0].i < 10 ? a[0].i : 0;
        return jni::Value{};
    });
    string(lib, "GetSDFolder", "()Ljava/lang/String;", data);
    r.register_method(lib, "GetMAC", "(I)Ljava/lang/String;", [](auto &vm, auto, const auto &a) {
        jni::Value v;
        v.l = vm.string(a[0].i == 1 ? profile::mac_digest() : profile::identifier("mac"));
        return v;
    });
    string(lib, "GetUDID", "(Z)Ljava/lang/String;", profile::identifier("udid").c_str());
    for (auto [method, id] : {std::pair{"getAndroidID", "android_id"}, std::pair{"getIMEI", "imei"},
                              std::pair{"getSerial", "serial"}, std::pair{"getSerialNo", "serial"}})
        string(lib, method, "()Ljava/lang/String;", profile::identifier(id).c_str());
    auto mac = profile::identifier("mac");
    mac.erase(std::remove(mac.begin(), mac.end(), ':'), mac.end());
    string(lib, "getMac", "()Ljava/lang/String;", mac.c_str());
    string("android/telephony/TelephonyManager", "getDeviceId", "()Ljava/lang/String;",
           profile::identifier("imei").c_str(), false);
    string("android/net/wifi/WifiInfo", "getMacAddress", "()Ljava/lang/String;",
           profile::identifier("mac").c_str(), false);
    r.register_method("android/provider/Settings$Secure", "getString",
                      "(Landroid/content/ContentResolver;Ljava/lang/String;)Ljava/lang/String;",
                      [](auto &vm, auto, const auto &a) {
                          jni::Value v;
                          if (vm.text(a[1].l) == "android_id")
                              v.l = vm.string(profile::identifier("android_id"));
                          return v;
                      });
    if(!mutable_extent) mutable_extent=std::make_shared<platform::GuestExtentState>(extent);
    r.register_method(lib,"GetWindowWidth","()I",[mutable_extent](auto &,auto,const auto &) {
        return number(mutable_extent->snapshot().width);
    });
    r.register_method(lib,"GetWindowHeight","()I",[mutable_extent](auto &,auto,const auto &) {
        return number(mutable_extent->snapshot().height);
    });
    // Provisional maximum callback-selected tiers; see docs/device-profile.md.
    integer(lib, "GetPhoneCPUCores", "()I", 4);
    integer(lib, "IsTegra4", "()Z", 1);
    integer(lib, "IsWifiEnabled", "()I", 1); // LAN socket capability; no Internet reachability claim.
    for (auto name : {"HasGyroscope", "Is3gEnabled", "IsKeyboardVisible", "UseEffectSpec"})
        integer(lib, name, "()I", 0);
    for (auto name : {"IsAmazonNub", "IsHTCPhone", "isSlideEnabled", "isZEUSDevice", "isCalibratingGyro"})
        integer(lib, name, "()Z", 0);
    r.register_method(lib, "IsFirstTimeLaunch", "(Ljava/lang/String;)Z", [](auto &vm, auto, const auto &a) {
        return number(profile::first_launch(vm.text(a[0].l)) ? 1 : 0);
    });
    r.register_method(lib, "IsNewDay", "()Z", [](auto &, auto, const auto &) {
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
        return number(profile::new_day(ms) ? 1 : 0);
    });
    integer(lib, "setCurrentContext", "(I)Z", 0);
    for (auto [name, value] : {std::pair{"GetPhoneCPUFreq", 2400.0f}, std::pair{"GetPhoneMemory", 2048.0f}})
        r.register_method(lib, name, "()F", [value](auto &, auto, const auto &) {
            jni::Value v;
            v.f = value;
            return v;
        });
    r.register_method(lib, "setupPaths", "()V", [](auto &vm, auto, const auto &) {
        paths(vm);
        return jni::Value{};
    });
    r.register_method(lib, "createView", "()V", [](auto &, auto, const auto &) {
        view_requested = true;
        std::printf("Java createView requested; pre-GL probe defers surface creation\n");
        return jni::Value{};
    });
    r.register_method(lib, "GetInt", "(Ljava/lang/String;I)I", [](auto &vm, auto, const auto &a) {
        return number(static_cast<int>(profile::get_number("MySavedData", vm.text(a[0].l), a[1].i)));
    });
    r.register_method(lib, "PutInt", "(Ljava/lang/String;I)V", [](auto &vm, auto, const auto &a) {
        profile::put_number("MySavedData", vm.text(a[0].l), a[1].i);
        return jni::Value{};
    });
    r.register_method(lib, "GetGameState", "()I",
                      [](auto &, auto, const auto &) { return number(game_state); });
    r.register_method(lib, "SetGameState", "(I)V", [](auto &, auto, const auto &a) {
        game_state = a[0].i;
        return jni::Value{};
    });
    for (auto [name, sig] :
         {std::pair{"getResource", "(Ljava/lang/String;)[B"},
          std::pair{"GetKeyboardText", "()[B"}})
        noop(lib, name, sig);
    r.register_method(lib, "GetFileFromURL", "(Ljava/lang/String;)[B", [](auto &vm, auto, const auto &a) {
        jni::Value result;
        if (a.size() != 1)
            return result;
        auto bytes = net::local_shop_file(vm.text(a[0].l));
        if (bytes)
            result.l = vm.bytes(bytes->data(), bytes->size());
        return result;
    });
    for (auto [name, sig] : {std::pair{"setNoSwapFrames", "(I)V"},
                             std::pair{"setViewSettings", "(IIIII)V"},
                             std::pair{"enableAccelerometer", "(ZF)V"},
                             std::pair{"enableGyroscope", "(ZF)V"},
                             std::pair{"setCalibratingGyro", "(Z)V"},
                             std::pair{"resetGyroCalibration", "()V"},
                             std::pair{"KeepScreenOn", "(I)V"},
                             std::pair{"ShowLoadingScreen", "(Z)V"},
                             std::pair{"ShowToast", "(Ljava/lang/String;)V"},
                             std::pair{"ShowAlert", "(Ljava/lang/String;Ljava/lang/String;)V"},
                             std::pair{"BrowserLaunch", "(Ljava/lang/String;)V"},
                             std::pair{"GLLiveLaunch", "(II)V"},
                             std::pair{"IGPLaunch", "(I)V"},
                             std::pair{"TrackAndroidHits", "(I)V"},
                             std::pair{"SetActionPhase", "(I)V"},
                             std::pair{"GLLiveNotifyTrophy", "(I)V"},
                             std::pair{"SendToBackground", "()V"},
                             std::pair{"SetKeyboard", "(ILjava/lang/String;I)V"},
                             std::pair{"GamePause", "()V"},
                             std::pair{"GameResume", "()V"},
                             std::pair{"resize", "(II)V"}})
        noop(lib, name, sig);
    const auto serial = profile::identifier("serial");
    for (auto [name, value] :
         {std::pair{"MODEL", "Nova3PC x86"}, std::pair{"MANUFACTURER", "Nova3PC"},
          std::pair{"BRAND", "Nova3PC"}, std::pair{"DEVICE", "x86"}, std::pair{"CPU_ABI", "x86"},
          std::pair{"PRODUCT", "nova3pc"}, std::pair{"SERIAL", serial.c_str()}}) {
        jni::Value v;
        v.l = r.string(value);
        r.register_field("android/os/Build", name, "Ljava/lang/String;", v);
        r.release(v.l, 1);
    }
    r.register_field("android/os/Build$VERSION", "SDK_INT", "I", number(9));
    for (auto [name, value] : {std::pair{"SDK", "9"}, std::pair{"RELEASE", "2.3.7"}}) {
        jni::Value v;
        v.l = r.string(value);
        r.register_field("android/os/Build$VERSION", name, "Ljava/lang/String;", v);
        r.release(v.l, 1);
    }
    r.register_method("android/os/SystemProperties", "get", "(Ljava/lang/String;)Ljava/lang/String;",
                      [](auto &vm, auto, const auto &a) {
                          auto key = vm.text(a[0].l);
                          jni::Value v;
                          v.l = vm.string(key == "ro.serialno" || key == "ril.serialnumber"
                                              ? profile::identifier("serial")
                                              : "");
                          return v;
                      });
}
void startup(jni::Runtime &r) {
    auto c = r.find_class(lib);
    // onCreate probes device controls before installer completion (i() calls twice).
    r.native(symbol + "nativeSetIsMOGA", c, {1});
    r.native(symbol + "nativeSetIsMOGA", c, {1});
    // Replay local installer native side effects; see DOMANDE_CODEX.md.
    const std::string installer_symbol =
        "Java_com_gameloft_android_ANMP_GloftN3HM_installer_GameInstaller_";
    const std::string policy_symbol = "Java_com_gameloft_android_ANMP_GloftN3HM_installer_GDRMPolicy_";
    auto installer_class = r.find_class(base + "installer/GameInstaller");
    auto policy_class = r.find_class(base + "installer/GDRMPolicy");
    auto installer = std::make_shared<jni::Object>();
    installer->klass = r.object(installer_class);
    auto installer_ref = r.reference(installer);
    auto policy = std::make_shared<jni::Object>();
    policy->klass = r.object(policy_class);
    auto policy_ref = r.reference(policy);
    auto discard_string = [&](uint32_t h) { r.release(reinterpret_cast<jni::Handle>(h), 1); };
    discard_string(r.native(installer_symbol + "getPublicKey", installer_ref));
    r.native(installer_symbol + "initNative", installer_class);
    constexpr uint32_t licensed =
        0x4e334c49; // Provisional stable Java enum identity, not a protocol value.
    r.native(policy_symbol + "initNativeAP", policy_class, {licensed, licensed, licensed});
    auto space_ref =
        reinterpret_cast<jni::Handle>(r.native(policy_symbol + "getConstString", policy_ref, {0}));
    policy_space = r.text(space_ref);
    r.release(space_ref, 1);
    uint64_t now = static_cast<uint64_t>(std::time(nullptr));
    std::string response;
    for (uint32_t i = 1; i <= 8; ++i) {
        auto key_ref =
            reinterpret_cast<jni::Handle>(r.native(policy_symbol + "getConstString", policy_ref, {i}));
        auto key = r.text(key_ref);
        r.release(key_ref, 1);
        auto fallback = i == 1   ? std::string("LICENSED")
                        : i == 6 ? std::string("true")
                                 : std::to_string(i == 2 ? now + 86400 : 0);
        auto stored = profile::get(policy_space, key, fallback);
        if (i == 1)
            response = stored;
        if (i == 2)
            stored = installer_host_validity(response, stored, static_cast<int64_t>(now));
        profile::put(policy_space, key, stored);
        if (i == 1 || i == 6)
            r.native(policy_symbol + "setConst", policy_ref,
                     {i, i == 1              ? licensed
                         : stored == "false" ? 0u
                                             : 1u});
        else {
            uint64_t value = static_cast<uint64_t>(profile::get_number(policy_space, key, 0));
            r.native(policy_symbol + "setLongConst", policy_ref,
                     {i, static_cast<uint32_t>(value), static_cast<uint32_t>(value >> 32)});
        }
    }
    std::vector<uint32_t> time_words = {static_cast<uint32_t>(now), static_cast<uint32_t>(now >> 32)};
    uint32_t allowed = r.native(policy_symbol + "nativeAllow", policy_ref, time_words);
    if (allowed != 1) {
        r.native(policy_symbol + "setTime", policy_ref, time_words);
        for (uint32_t i = 0; i < 3; ++i)
            r.native(policy_symbol + "processServer", policy_ref, {i, licensed});
    }
    if (allowed != 1)
        allowed = r.native(policy_symbol + "nativeAllow", policy_ref, time_words);
    std::printf("installer nativeAllow=%u\n", allowed);
    if (allowed != 1)
        throw std::runtime_error("Installer policy did not accept the licensed host values");
    r.native(installer_symbol + "nativeStart", installer_class);
    r.release(installer_ref, 1);
    r.release(policy_ref, 1);
    r.release(installer_class, 1);
    r.release(policy_class, 1);
    // These Java class initializers populate native cached class/method IDs.
    // They precede GL2JNILib.init in the activity's one-time init helper.
    auto push_class = r.find_class(base + "PushNotification/C2DMAndroidUtils");
    r.native("Java_com_gameloft_android_ANMP_GloftN3HM_PushNotification_C2DMAndroidUtils_nativeInit",
             push_class);
    r.release(push_class, 1);
    auto billing_class = r.find_class(base + "iab/InAppBilling");
    auto activity_object = std::make_shared<jni::Object>();
    activity_object->klass = r.klass(activity);
    auto activity_ref = r.reference(activity_object);
    r.native("Java_com_gameloft_android_ANMP_GloftN3HM_iab_InAppBilling_nativeInit",
             billing_class, {reinterpret_cast<uint32_t>(activity_ref)});
    r.release(activity_ref, 1);
    r.release(billing_class, 1);
    r.native(symbol + "init", c);
    r.native(symbol + "nativeSetIsMOGA", c, {1});
    r.native(symbol + "nativeSetIsMOGA", c, {1});
    r.native(symbol + "nativeSetXperiaPlay", c, {0});
    r.native(symbol + "nativeSetSO01D", c, {0});
    r.native(symbol + "nativeIsSlideChanged", c, {0});
    r.native(symbol + "nativeSetIsMOGA", c, {1});
    r.native(symbol + "nativeSetIsMOGA", c, {1});
    std::printf("pre-GL startup complete; createView_requested=%d; next native=initGL\n",
                view_requested ? 1 : 0);
    r.release(c, 1);
}
} // namespace nova::java
