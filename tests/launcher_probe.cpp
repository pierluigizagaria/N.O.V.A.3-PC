#include "launcher/setup.h"
#include "launcher/process.h"
#include "launcher/language.h"
#include "launcher/shortcut.h"
#include <shellapi.h>
#include <fstream>
#include <iostream>
using namespace nova::launcher;
int wmain(int argc,wchar_t **argv) {
    try {
        if(argc==2 && std::wstring(argv[1])==L"language") {
            const std::pair<std::wstring,Language> locales[]{
                {L"IT-it",Language::italian},{L"it_CH",Language::italian},{L"en-US",Language::english},
                {L"fr-CA",Language::french},{L"de-DE",Language::german},{L"es-MX",Language::spanish},
                {L"pt-BR",Language::portuguese},{L"pt-PT",Language::portuguese},{L"ja-JP",Language::english},
                {L"",Language::english}};
            for(const auto &[locale,expected]:locales) if(language_for_locale(locale)!=expected) return 40;
            for(int lang=0;lang<6;++lang) for(int id=0;id<static_cast<int>(Text::count);++id)
                if(!*tr(static_cast<Language>(lang),static_cast<Text>(id))) return 41;
            if(localized_log(Language::italian,"Preparing 2/40: strings.gla")!=L"Preparazione: 2/40: strings.gla") return 42;
            if(localized_log(Language::italian,"Verifying main.obb")!=L"Verifica: main.obb") return 43;
            if(localized_log(Language::english,"Verifying main.obb")!=L"Verifying main.obb") return 44;
            // The picker's saved two-letter code must map back to the same language.
            for(int lang=0;lang<language_count;++lang)
                if(language_for_locale(language_code(static_cast<Language>(lang)))!=static_cast<Language>(lang)) return 45;
            return 0;
        }
        if(argc==4 && std::wstring(argv[1])==L"shortcut") {
            create_game_shortcut(Layout{argv[2]},argv[3]); return 0;
        }
        if(argc>=2 && std::wstring(argv[1])==L"quote") {
            for(const std::wstring value:{L"",L"with space",L"C:\\space here\\",L"a\\\"b",L"plain",L"\u6e38\u620f"}) {
                auto command=L"program "+quote_argument(value); int count=0;
                auto parsed=CommandLineToArgvW(command.c_str(),&count);
                const bool good=parsed && count==2 && parsed[1]==value;
                if(parsed) LocalFree(parsed);
                if(!good) throw std::runtime_error("Argument roundtrip failed.");
            }
            return 0;
        }
        if(argc==4 && std::wstring(argv[1])==L"launch") {
            Layout layout{argv[2]}; auto process=start_game(layout,argv[3]);
            if(WaitForSingleObject(process,15000)!=WAIT_OBJECT_0) {CloseHandle(process); return 20;}
            DWORD code=21; GetExitCodeProcess(process,&code); CloseHandle(process); return static_cast<int>(code);
        }
        if(argc!=6) return 2;
        Layout layout{argv[2]}; Inputs inputs;
        std::ifstream specification(argv[3]);
        specification>>inputs.apk_sha256>>inputs.library.name>>inputs.library.sha256>>inputs.library.size
            >>inputs.main.name>>inputs.main.sha256>>inputs.main.size>>inputs.patch.name>>inputs.patch.sha256>>inputs.patch.size;
        std::string name;
        while(specification>>name) inputs.bootstrap.push_back(name);
        if(inputs.bootstrap.empty()) return 3;
        if(std::wstring(argv[1])==L"ready") return ready(layout,inputs)?0:10;
        std::atomic_bool cancel=false;
        const auto mode=std::wstring(argv[1]);
        bool importing=false,bootstrap=false;
        Progress progress{
            [&](const std::string &message) {std::cout<<message<<'\n'; importing=message.starts_with("Importing"); bootstrap=message.starts_with("Preparing");},
            [&](unsigned percent) {
                if(percent>10 && (mode==L"cancel" || (mode==L"cancel-write" && importing) ||
                   (mode==L"cancel-bootstrap" && bootstrap))) cancel=true;
            },&cancel};
        prepare(layout,std::wstring(argv[4])==L"-"?fs::path{}:fs::path(argv[4]),
            std::wstring(argv[5])==L"-"?fs::path{}:fs::path(argv[5]),progress,inputs);
        return ready(layout,inputs)?0:11;
    } catch(const InputRequired &e) {std::cerr<<e.what(); return e.kind==RequiredInput::apk?12:13;}
    catch(const Cancelled &e) {std::cerr<<e.what(); return 14;}
    catch(const std::exception &e) {std::cerr<<e.what(); return 1;}
}
