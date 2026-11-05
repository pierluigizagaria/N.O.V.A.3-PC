#include "launcher/setup.h"
#include <windows.h>
#include <bcrypt.h>
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <memory>
#include <set>
#include <sstream>

namespace nova::launcher {
namespace {
constexpr size_t chunk_size = 128 * 1024;
[[noreturn]] void fail(const std::string &s) { throw std::runtime_error(s); }
class File {
    HANDLE handle_ = INVALID_HANDLE_VALUE;
public:
    explicit File(const fs::path &p, bool write=false) {
        handle_=CreateFileW(p.c_str(),write ? GENERIC_WRITE : GENERIC_READ,
            write ? 0 : FILE_SHARE_READ,nullptr,write ? CREATE_NEW : OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,nullptr);
        if(handle_==INVALID_HANDLE_VALUE) fail("Cannot open file: " + p.filename().string() +
            " (Windows error " + std::to_string(GetLastError()) + ").");
    }
    ~File() { if(handle_!=INVALID_HANDLE_VALUE) CloseHandle(handle_); }
    File(const File &)=delete;
    File &operator=(const File &)=delete;
    uint64_t size() { LARGE_INTEGER n{}; if(!GetFileSizeEx(handle_,&n)) fail("Cannot read file size."); return n.QuadPart; }
    void seek(uint64_t offset) {
        LARGE_INTEGER n{}; n.QuadPart=static_cast<LONGLONG>(offset);
        if(!SetFilePointerEx(handle_,n,nullptr,FILE_BEGIN)) fail("Cannot seek in archive.");
    }
    void read(void *data,size_t n) {
        DWORD done=0;
        if(n>MAXDWORD || !ReadFile(handle_,data,static_cast<DWORD>(n),&done,nullptr) || done!=n)
            fail("Truncated or unreadable archive.");
    }
    void write(const void *data,size_t n) {
        DWORD done=0;
        if(n>MAXDWORD || !WriteFile(handle_,data,static_cast<DWORD>(n),&done,nullptr) || done!=n)
            fail("Cannot write setup files. Check free disk space and folder permissions.");
    }
    void flush() { if(!FlushFileBuffers(handle_)) fail("Cannot flush setup file."); }
};
class Hash {
    BCRYPT_ALG_HANDLE algorithm_=nullptr;
    BCRYPT_HASH_HANDLE hash_=nullptr;
public:
    Hash() {
        if(BCryptOpenAlgorithmProvider(&algorithm_,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)
            fail("SHA-256 is unavailable.");
        if(BCryptCreateHash(algorithm_,&hash_,nullptr,0,nullptr,0,0)<0) {
            BCryptCloseAlgorithmProvider(algorithm_,0); fail("Cannot create SHA-256 state.");
        }
    }
    ~Hash() { BCryptDestroyHash(hash_); BCryptCloseAlgorithmProvider(algorithm_,0); }
    void add(const void *data,size_t n) {
        if(BCryptHashData(hash_,static_cast<PUCHAR>(const_cast<void *>(data)),static_cast<ULONG>(n),0)<0)
            fail("SHA-256 failed.");
    }
    std::string finish() {
        std::array<unsigned char,32> digest{};
        if(BCryptFinishHash(hash_,digest.data(),static_cast<ULONG>(digest.size()),0)<0) fail("SHA-256 failed.");
        std::ostringstream s; s<<std::hex<<std::setfill('0');
        for(auto c:digest) s<<std::setw(2)<<static_cast<unsigned>(c);
        return s.str();
    }
};
std::string sha(File &file,const Progress &progress) {
    Hash hash; std::array<unsigned char,chunk_size> bytes{};
    file.seek(0); const auto total=file.size(); uint64_t done=0;
    while(done<total) {
        progress.check(); const auto n=static_cast<size_t>(std::min<uint64_t>(bytes.size(),total-done));
        file.read(bytes.data(),n); hash.add(bytes.data(),n); done+=n; progress.update(done,total);
    }
    return hash.finish();
}
void safe_destination(const Layout &layout,const fs::path &destination) {
    const auto base=fs::absolute(layout.root).lexically_normal();
    const auto target=fs::absolute(destination).lexically_normal();
    const auto relative=target.lexically_relative(base);
    if(relative.empty() || *relative.begin()!=L"local") fail("Setup output must stay under local/.");
    auto current=base;
    for(const auto &part:relative) {
        if(part==L"..") fail("Setup path escapes local/.");
        current/=part;
        auto flags=GetFileAttributesW(current.c_str());
        if(flags!=INVALID_FILE_ATTRIBUTES && (flags&FILE_ATTRIBUTE_REPARSE_POINT))
            fail("Setup refuses links or junctions inside local/.");
    }
}
class Output {
    fs::path temporary_, destination_;
    std::unique_ptr<File> file_;
public:
    Output(const Layout &layout,const fs::path &destination):destination_(destination) {
        safe_destination(layout,destination); fs::create_directories(destination.parent_path());
        static std::atomic_uint serial=0;
        // CREATE_NEW prevents accidental replacement of stale temporary files.
        for(unsigned i=0;i<100;++i) {
            temporary_=destination;
            temporary_+=L".part-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(++serial);
            if(!fs::exists(temporary_)) { file_=std::make_unique<File>(temporary_,true); return; }
        }
        fail("Cannot allocate a setup temporary file.");
    }
    ~Output() { file_.reset(); std::error_code ec; fs::remove(temporary_,ec); }
    void write(const void *p,size_t n) { file_->write(p,n); }
    void commit() {
        file_->flush(); file_.reset();
        if(!MoveFileExW(temporary_.c_str(),destination_.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            fail("Cannot publish prepared file (Windows error "+std::to_string(GetLastError())+").");
    }
};
uint16_t u16(const unsigned char *b) { return uint16_t(b[0]) | uint16_t(b[1])<<8; }
uint32_t u32(const unsigned char *b) { return uint32_t(u16(b)) | uint32_t(u16(b+2))<<16; }
struct Entry { std::string name; uint32_t crc,packed,size,offset; uint16_t method,flags; };
class Zip {
    std::unique_ptr<File> file_;
    uint64_t directory_=0;
public:
    std::vector<Entry> entries;
    explicit Zip(const fs::path &path):file_(std::make_unique<File>(path)) {
        const auto total=file_->size(); if(total<22) fail("Not a ZIP/APK archive.");
        const auto tail_size=static_cast<size_t>(std::min<uint64_t>(total,65557));
        std::vector<unsigned char> tail(tail_size); file_->seek(total-tail_size); file_->read(tail.data(),tail.size());
        size_t end=tail_size;
        for(size_t pos=tail_size-22;;--pos) {
            if(u32(tail.data()+pos)==0x06054b50 && pos+22+u16(tail.data()+pos+20)==tail_size) { end=pos; break; }
            if(pos==0) break;
        }
        if(end==tail_size) fail("Archive directory is missing.");
        auto e=tail.data()+end;
        const auto count=u16(e+10); const uint64_t length=u32(e+12); directory_=u32(e+16);
        if(u16(e+4) || u16(e+6) || u16(e+8)!=count || count==65535 ||
           directory_==0xffffffff || length==0xffffffff || directory_+length>total-tail_size+end)
            fail("Unsupported split or ZIP64 archive.");
        if(length>64*1024*1024) fail("Archive directory is too large.");
        std::vector<unsigned char> bytes(static_cast<size_t>(length)); file_->seek(directory_);
        file_->read(bytes.data(),bytes.size()); size_t position=0; std::set<std::string> names;
        for(unsigned i=0;i<count;++i) {
            if(position+46>bytes.size()) fail("Truncated ZIP directory.");
            auto b=bytes.data()+position;
            if(u32(b)!=0x02014b50) fail("Invalid ZIP directory.");
            const size_t n=u16(b+28), extra=u16(b+30), comment=u16(b+32);
            if(position+46+n+extra+comment>bytes.size()) fail("Truncated ZIP member.");
            Entry entry{std::string(reinterpret_cast<char *>(b+46),n),u32(b+16),u32(b+20),u32(b+24),u32(b+42),u16(b+10),u16(b+8)};
            if(entry.name.find('\0')!=std::string::npos || !names.insert(entry.name).second)
                fail("Ambiguous ZIP member names.");
            if(u16(b+34) || entry.size==0xffffffff || entry.packed==0xffffffff || entry.offset==0xffffffff)
                fail("Unsupported ZIP64 member.");
            entries.push_back(std::move(entry)); position+=46+n+extra+comment;
        }
        if(position!=bytes.size()) fail("Unexpected ZIP directory data.");
    }
    File &file() { return *file_; }
    const Entry *find(const std::string &name,bool basename=false) const {
        const Entry *result=nullptr;
        for(const auto &e:entries) {
            auto key=e.name;
            if(basename) { const auto p=key.find_last_of("/\\"); if(p!=std::string::npos) key=key.substr(p+1); }
            if(key==name) { if(result) fail("More than one matching "+name+" in archive."); result=&e; }
        }
        return result;
    }
    void extract(const Entry &entry,const Layout &layout,const fs::path &target,
                 const std::string &expected,const Progress &progress) {
        if((entry.flags&~uint16_t(0x080e)) || (entry.method!=0 && entry.method!=8))
            fail("Encrypted or unsupported ZIP member.");
        std::array<unsigned char,30> header{};
        if(uint64_t(entry.offset)+header.size()>directory_) fail("Invalid member offset.");
        file_->seek(entry.offset); file_->read(header.data(),header.size());
        if(u32(header.data())!=0x04034b50 || u16(header.data()+8)!=entry.method || u16(header.data()+6)!=entry.flags)
            fail("ZIP headers disagree.");
        const auto name_size=u16(header.data()+26),extra_size=u16(header.data()+28);
        const uint64_t data_offset=uint64_t(entry.offset)+30+name_size+extra_size;
        if(data_offset+entry.packed>directory_) fail("ZIP data escapes archive bounds.");
        std::string name(name_size,'\0'); file_->read(name.data(),name.size());
        if(name!=entry.name) fail("ZIP local name disagrees with directory.");
        file_->seek(data_offset);
        Output output(layout,target); Hash hash;
        std::array<unsigned char,chunk_size> input{},decoded{};
        uint64_t consumed=0,written=0; uLong crc=crc32(0,nullptr,0);
        auto emit=[&](const void *data,size_t n) {
            if(written+n>entry.size) fail("ZIP member exceeds declared size.");
            output.write(data,n); hash.add(data,n);
            crc=crc32(crc,static_cast<const Bytef *>(data),static_cast<uInt>(n)); written+=n;
            progress.update(written,entry.size); progress.check();
        };
        z_stream stream{};
        struct End { z_stream *s; ~End() { if(s) inflateEnd(s); } } end{nullptr};
        if(entry.method==8) {
            if(inflateInit2(&stream,-MAX_WBITS)!=Z_OK) fail("Cannot initialize ZIP decompression.");
            end.s=&stream;
        }
        bool finished=false;
        while(consumed<entry.packed) {
            progress.check(); const auto n=static_cast<size_t>(std::min<uint64_t>(input.size(),entry.packed-consumed));
            file_->read(input.data(),n); consumed+=n;
            if(entry.method==0) { emit(input.data(),n); continue; }
            stream.next_in=input.data(); stream.avail_in=static_cast<uInt>(n);
            do {
                stream.next_out=decoded.data(); stream.avail_out=static_cast<uInt>(decoded.size());
                const auto result=inflate(&stream,Z_NO_FLUSH);
                const auto produced=decoded.size()-stream.avail_out;
                if(result!=Z_OK && result!=Z_STREAM_END) fail("Corrupt compressed ZIP member.");
                emit(decoded.data(),produced);
                if(result==Z_STREAM_END) {
                    if(stream.avail_in || consumed!=entry.packed) fail("Trailing compressed ZIP data.");
                    finished=true; break;
                }
                if(!stream.avail_in && stream.avail_out) break;
            } while(true);
            if(finished) break;
        }
        if((entry.method==8 && !finished) || written!=entry.size || crc!=entry.crc)
            fail("ZIP size or CRC verification failed.");
        if(!expected.empty() && hash.finish()!=expected) fail("SHA-256 mismatch: "+entry.name);
        progress.check(); output.commit();
    }
};
bool matches(const fs::path &path,const Payload &payload,const Progress &progress) {
    if(!fs::is_regular_file(path) || fs::file_size(path)!=payload.size) return false;
    progress.begin("Verifying "+payload.name); File file(path); return sha(file,progress)==payload.sha256;
}
bool crc_matches(const fs::path &path,const Entry &entry,const Progress &progress) {
    if(!fs::is_regular_file(path) || fs::file_size(path)!=entry.size) return false;
    File file(path); std::array<unsigned char,chunk_size> bytes{}; uint64_t done=0; uLong crc=crc32(0,nullptr,0);
    while(done<entry.size) {
        progress.check(); const auto n=static_cast<size_t>(std::min<uint64_t>(bytes.size(),entry.size-done));
        file.read(bytes.data(),n); crc=crc32(crc,bytes.data(),static_cast<uInt>(n)); done+=n; progress.update(done,entry.size);
    }
    return crc==entry.crc;
}
void copy_payload(const Layout &layout,const fs::path &source,const fs::path &target,
                  const Payload &payload,const Progress &progress) {
    progress.begin("Importing "+payload.name); File file(source);
    if(file.size()!=payload.size) fail("Incorrect size for "+payload.name+". Select the supported 1.0.7 data.");
    Output output(layout,target); Hash hash; std::array<unsigned char,chunk_size> bytes{};
    uint64_t done=0;
    while(done<payload.size) {
        progress.check(); const auto n=static_cast<size_t>(std::min<uint64_t>(bytes.size(),payload.size-done));
        file.read(bytes.data(),n); hash.add(bytes.data(),n); output.write(bytes.data(),n); done+=n; progress.update(done,payload.size);
    }
    if(hash.finish()!=payload.sha256) fail("SHA-256 mismatch: "+payload.name);
    progress.check(); output.commit();
}
std::vector<fs::path> tracked(const Layout &layout,const Inputs &inputs) {
    std::vector<fs::path> paths{layout.library(),layout.obb()/inputs.main.name,layout.obb()/inputs.patch.name};
    for(const auto &name:inputs.bootstrap) {
        // This is a compiled allowlist, never an archive-selected output path.
        if(name.empty() || name.find_first_of("/\\:")!=std::string::npos || name=="." || name=="..")
            fail("Invalid bootstrap allowlist.");
        paths.push_back(layout.resources()/name);
    }
    return paths;
}
std::string receipt_text(const Layout &layout,const Inputs &inputs) {
    std::ostringstream s;
    s<<"NOVA-SETUP-1\n"<<inputs.library.sha256<<'\n'<<inputs.main.sha256<<'\n'<<inputs.patch.sha256<<'\n';
    for(const auto &path:tracked(layout,inputs)) {
        safe_destination(layout,path);
        if(!fs::is_regular_file(path) || !fs::file_size(path)) fail("Prepared file is missing or empty.");
        s<<path.lexically_relative(layout.root).generic_string()<<'\t'<<fs::file_size(path)<<'\t'
            <<fs::last_write_time(path).time_since_epoch().count()<<'\n';
    }
    return s.str();
}
}

const Inputs &known_inputs() {
    static const Inputs inputs{
        "7f6a0e1add98a7bd2bba8138b57b5cec22783eeb0a03d0cf75c8f5404d2da372",
        {"lib/x86/libNOVA3_neon.so","a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e",21614897},
        {"main.1050.com.gameloft.android.ANMP.GloftN3HM.obb","0e5d025cbe1a4c51d6071993cf402166191bf69901a0e0d9896235aeda2b2c14",1980258754},
        {"patch.1070.com.gameloft.android.ANMP.GloftN3HM.obb","3b1806a3180070f76695be3fe052b40429ab6dc8c9d33fd9c4baf6db2219e738",339002046},
        {
#include "bootstrap_files.inc"
        }};
    return inputs;
}
void Progress::check() const { if(cancel && cancel->load()) throw Cancelled(); }
void Progress::begin(const std::string &message) const { check(); if(log) log(message); update(0,1); }
void Progress::update(uint64_t done,uint64_t total) const {
    if(percent) percent(total ? static_cast<unsigned>(std::min<uint64_t>(100,done*100/total)) : 100);
}
fs::path Layout::library() const { return root/"local/input/lib/x86/libNOVA3_neon.so"; }
fs::path Layout::obb() const { return root/"local/game/obb"; }
fs::path Layout::profile() const { return root/"local/profiles/player"; }
fs::path Layout::resources() const { return profile()/"sdcard/Android/data/com.gameloft.android.ANMP.GloftN3HM/files"; }
fs::path Layout::receipt() const { return root/"local/launcher/ready.txt"; }
bool ready(const Layout &layout,const Inputs &inputs) {
    try {
        safe_destination(layout,layout.receipt());
        if(!fs::is_regular_file(layout.receipt()) || fs::file_size(layout.receipt())>65536) return false;
        std::ifstream file(layout.receipt(),std::ios::binary);
        const std::string text{std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};
        return file.good() && text==receipt_text(layout,inputs);
    } catch(const std::exception &) { return false; }
}
void prepare(const Layout &layout,const fs::path &apk,const fs::path &data,
             const Progress &progress,const Inputs &inputs) {
    progress.check();
    for(const auto &path:tracked(layout,inputs)) safe_destination(layout,path);
    safe_destination(layout,layout.receipt());
    // Invalidate completion before any repair; a failure must never publish readiness.
    std::error_code ec; fs::remove(layout.receipt(),ec);
    if(ec) fail("Cannot invalidate previous setup receipt.");
    const bool library_ready=matches(layout.library(),inputs.library,progress);
    if(!library_ready && apk.empty())
        throw InputRequired(RequiredInput::apk,"Select your N.O.V.A. 3 1.0.7 x86 APK.");
    // A deliberate selection must always be validated, including reconfiguration.
    if(!apk.empty()) {
        progress.begin("Checking APK compatibility"); Zip archive(apk);
        if(sha(archive.file(),progress)!=inputs.apk_sha256)
            fail("This APK is not the supported N.O.V.A. 3 1.0.7 x86 build. Select the compatible APK.");
        const auto entry=archive.find(inputs.library.name);
        if(!entry || entry->size!=inputs.library.size) fail("The APK is missing the supported x86 library.");
        if(!library_ready) {
            progress.begin("Importing the game library"); archive.extract(*entry,layout,layout.library(),inputs.library.sha256,progress);
        }
        if(inputs.apk_sha256==known_inputs().apk_sha256) {
            if(const auto icon=archive.find("res/drawable-xxhdpi/icon.png");
               icon && !fs::is_regular_file(layout.root/"local/launcher/icon.png")) {
                progress.begin("Importing icon.png");
                archive.extract(*icon,layout,layout.root/"local/launcher/icon.png","",progress);
            }
        }
    }
    std::vector<const Payload *> missing;
    for(const auto *payload:{&inputs.main,&inputs.patch})
        if(!matches(layout.obb()/payload->name,*payload,progress)) missing.push_back(payload);
    if(!missing.empty()) {
        fs::path selected=data;
        if(selected.empty() && !apk.empty()) {
            const auto folder=apk.parent_path();
            bool all=true;
            for(const auto *payload:missing) all=all && fs::is_regular_file(folder/payload->name);
            if(all) selected=folder;
            else {
                std::vector<fs::path> candidates;
                unsigned scanned=0;
                for(const auto &item:fs::directory_iterator(folder)) {
                    if(++scanned>256) break;
                    progress.check();
                    if(!item.is_regular_file()) continue;
                    auto extension=item.path().extension().wstring();
                    std::transform(extension.begin(),extension.end(),extension.begin(),[](wchar_t c){return static_cast<wchar_t>(towlower(c));});
                    if(extension!=L".zip") continue;
                    try {
                        Zip archive(item.path()); bool has=true;
                        for(const auto *payload:missing) has=has && archive.find(payload->name,true)!=nullptr;
                        if(has) candidates.push_back(item.path());
                    } catch(const std::runtime_error &) { /* Not a usable data archive; request selection below. */ }
                }
                if(candidates.size()==1) selected=candidates.front();
            }
        }
        if(selected.empty()) throw InputRequired(RequiredInput::data,"The APK needs its main and patch data. Select the data ZIP or either OBB file.");
        auto folder=fs::is_directory(selected) ? selected : selected.parent_path();
        if(fs::is_directory(selected) || selected.extension()==L".obb") {
            for(const auto *payload:missing) copy_payload(layout,folder/payload->name,layout.obb()/payload->name,*payload,progress);
        } else {
            Zip archive(selected);
            for(const auto *payload:missing) {
                const auto entry=archive.find(payload->name,true);
                if(!entry || entry->size!=payload->size) fail("The selected archive is missing the supported "+payload->name+".");
                progress.begin("Importing "+payload->name);
                archive.extract(*entry,layout,layout.obb()/payload->name,payload->sha256,progress);
            }
        }
    }
    Zip main(layout.obb()/inputs.main.name),patch(layout.obb()/inputs.patch.name);
    for(size_t i=0;i<inputs.bootstrap.size();++i) {
        const auto &name=inputs.bootstrap[i]; const Entry *entry=main.find(name);
        if(!entry) fail("Main data is missing "+name+".");
        auto *archive=&main;
        if(auto replacement=patch.find(name)) { entry=replacement; archive=&patch; }
        progress.begin("Preparing "+std::to_string(i+1)+"/"+std::to_string(inputs.bootstrap.size())+": "+name);
        const auto target=layout.resources()/name;
        if(!crc_matches(target,*entry,progress)) archive->extract(*entry,layout,target,"",progress);
    }
    const auto sentinel=layout.resources()/"nothing.txt";
    safe_destination(layout,sentinel);
    if(!fs::exists(sentinel)) { Output output(layout,sentinel); constexpr char text[]="DoNotDeleteThisFile"; output.write(text,sizeof(text)-1); output.commit(); }
    if(inputs.apk_sha256==known_inputs().apk_sha256) {
        // Exact known main/patch sprites.gla font slices; publish only after SHA-256.
        // Original assets remain local-only and are never linked into the launcher.
        File sprites(layout.resources()/"sprites.gla");
        for(const uint64_t offset:{23140259u,23140106u}) {
            constexpr size_t length=48048;
            if(sprites.size()<offset+length) continue;
            std::array<unsigned char,length> font{}; sprites.seek(offset); sprites.read(font.data(),font.size());
            Hash hash; hash.add(font.data(),font.size());
            if(hash.finish()!="da2e59f53afef9135ca6c1d6109126ced1a67a41fb426176be8963982b684ea0") continue;
            const auto target=layout.root/"local/launcher/neuropol.ttf";
            const Payload payload{"neuropol.ttf","da2e59f53afef9135ca6c1d6109126ced1a67a41fb426176be8963982b684ea0",length};
            if(!matches(target,payload,progress)) {Output output(layout,target); output.write(font.data(),font.size()); output.commit();}
            break;
        }
    }
    progress.check(); const auto receipt=receipt_text(layout,inputs);
    Output output(layout,layout.receipt()); output.write(receipt.data(),receipt.size()); progress.check(); output.commit();
    progress.begin("Ready. Starting N.O.V.A. 3..."); progress.update(1,1);
}
std::wstring quote_argument(const std::wstring &value) {
    std::wstring result=L"\""; size_t slashes=0;
    for(auto c:value) {
        if(c==L'\\') { ++slashes; continue; }
        if(c==L'"') result.append(slashes*2+1,L'\\'); else result.append(slashes,L'\\');
        slashes=0; result+=c;
    }
    result.append(slashes*2,L'\\'); result+=L'"'; return result;
}
std::wstring game_arguments(const Layout &layout,const fs::path &executable) {
    // The host currently has a narrow main(). ASCII paths relative to its explicit
    // Unicode working directory avoid corrupting non-ACP installation paths.
    return quote_argument(executable.wstring())+L" "+quote_argument(layout.library().lexically_relative(layout.root).wstring())+
        L" --profile player --profile-root "+quote_argument(layout.profile().lexically_relative(layout.root).wstring())+
        L" --obb-dir "+quote_argument(layout.obb().lexically_relative(layout.root).wstring());
}
fs::path installation_root(const fs::path &directory) {
    // Repository builds and local/play reuse the established player profile.
    auto candidate=fs::absolute(directory);
    for(unsigned i=0;i<5;++i) {
        if(fs::is_regular_file(candidate/"tools/setup_inputs.py") && fs::is_regular_file(candidate/"src/play.cpp")) return candidate;
        if(candidate==candidate.parent_path()) break;
        candidate=candidate.parent_path();
    }
    return fs::absolute(directory); // Portable distribution: local/ beside both EXEs.
}
} // namespace nova::launcher
