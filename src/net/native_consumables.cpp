#include "net/native_consumables.h"
#include "abi/guest_hook.h"
#include "net/local_shop.h"
#include "net/native_server_tls.h"
#include <array>
#include <windows.h>
#include <bcrypt.h>
#include <cstdio>

namespace nova::net {
namespace {
template<class T=uint32_t> T read(uint32_t p,uint32_t offset=0) { return *reinterpret_cast<const T*>(p+offset); }
}
struct NativeConsumables::Impl {
    static Impl* active;
    static thread_local bool approved;
    static thread_local bool shopping, reconciled;
    bool (*dedicated)();
    uint32_t manager_slot,level_slot,update_frames,stock_slot,get_application,set_coins;
    std::array<uint32_t,9> pending{};
    std::array<uint32_t,9> before{};
    std::array<std::string,9> purchase_nonce{};
    std::optional<ConsumableInventory> inventory;
    std::unique_ptr<abi::GuestHook> activate,use,request,notify;
    std::unique_ptr<abi::GuestCall> menu_create;
    std::unique_ptr<abi::GuestHook> menu_update,save,coins;
    Impl(const ElfImage& image,bool (*mode)()):dedicated(mode) {
        manager_slot=image.symbol("_ZN13GameMpManager9SingletonE");
        level_slot=image.symbol("_ZN6CLevel10m_instanceE");
        stock_slot=image.symbol("_ZN11Consumables9SingletonE");
        get_application=abi::require_function(image,"_ZN11Application11GetInstanceEv");
        set_coins=abi::require_function(image,"_ZN11Application13SetCoinsCountEi");
        update_frames=abi::require_function(image,"_ZN21CLevelInterfaceNormal23UpdateConsumablesFramesEb");
        activate=std::make_unique<abi::GuestHook>(image,"_ZN11Consumables18ActivateConsumableE12EConsumables",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&activate_entry));
        use=std::make_unique<abi::GuestHook>(image,"_ZN11Consumables11ActivateUseEi12EConsumables",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&use_entry));
        request=std::make_unique<abi::GuestHook>(image,"_ZN13GameMpManager22MP_ClientInformsServerEii",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x18},reinterpret_cast<uint32_t>(&request_entry));
        notify=std::make_unique<abi::GuestHook>(image,"_ZN13GameMpManager19MP_ClientInformsAllEii",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x83,0xec,0x18},reinterpret_cast<uint32_t>(&notify_entry));
        // Create's lifecycle prologue belongs to native UI scaling. Refresh at
        // its first consumable-price query, before the original shop populates.
        menu_create=std::make_unique<abi::GuestCall>(image,"_ZN13GS_InGameMenu6CreateEv",0xdd6,
            "_ZNK11Consumables8GetPriceEi",reinterpret_cast<uint32_t>(&menu_price_entry));
        menu_update=std::make_unique<abi::GuestHook>(image,"_ZN13GS_InGameMenu15UpdatePausePageEv",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&menu_entry));
        save=std::make_unique<abi::GuestHook>(image,"_ZN11Consumables4SaveEv",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56,0x53},reinterpret_cast<uint32_t>(&save_entry));
        coins=std::make_unique<abi::GuestHook>(image,"_ZN11Application16ModifyCoinsCountEi",
            std::initializer_list<uint8_t>{0x55,0x89,0xe5,0x57,0x56},reinterpret_cast<uint32_t>(&coins_entry));
        active=this;
    }
    ~Impl(){coins.reset();save.reset();menu_update.reset();menu_create.reset();notify.reset();request.reset();use.reset();activate.reset();active=nullptr;}
    void apply_inventory(const ConsumableInventory& value) {
        const auto stock=read(stock_slot);
        if(stock)for(size_t i=0;i<9;++i)*reinterpret_cast<uint32_t*>(stock+i*4)=value.counts[i];
        const auto app=abi::invoke(get_application,{});
        if(app)abi::invoke(set_coins,{app,value.coins});
    }
    static uint32_t __cdecl menu_price_entry(uint32_t self,uint32_t type) {
        auto& s=*active;
        if(auto state=server_consumables())s.apply_inventory(*state);
        return abi::invoke(s.menu_create->original(),{self,type});
    }
    static void __cdecl menu_entry(uint32_t self) {
        auto& s=*active;const auto stock=read(s.stock_slot);
        // Authenticated shop ownership continues outside an active match.
        if(!NativeServerTls::login_credential() || !stock){abi::invoke(s.menu_update->original(),{self});return;}
        for(size_t i=0;i<9;++i)s.before[i]=read(stock,i*4);
        shopping=true;reconciled=false;s.inventory.reset();
        abi::invoke(s.menu_update->original(),{self});
        shopping=false;
        if(s.inventory)s.apply_inventory(*s.inventory);
    }
    static void __cdecl save_entry(uint32_t self) {
        auto& s=*active;
        if(shopping && !approved && !reconciled) {
            int kind=-1;bool valid=true;
            for(int i=0;i<9;++i) {
                const auto count=read(self,i*4);
                if(count!=s.before[i]) {
                    if(kind!=-1 || s.before[i]>=2147483647 || count!=s.before[i]+1)valid=false;
                    kind=i;
                }
            }
            if(valid && kind>=0) {
                reconciled=true;
                // The original pause callback increments then saves before
                // debiting coins. Replace that explicit buy with one atomic
                // authenticated purchase; never infer intent from a profile.
                for(size_t i=0;i<9;++i)*reinterpret_cast<uint32_t*>(self+i*4)=s.before[i];
                auto& nonce=s.purchase_nonce[kind];
                if(nonce.empty()) {
                    std::array<unsigned char,16> random{};
                    if(BCryptGenRandom(nullptr,random.data(),static_cast<ULONG>(random.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)==0) {
                        constexpr char hex[]="0123456789abcdef";
                        for(auto byte:random){nonce+=hex[byte>>4];nonce+=hex[byte&15];}
                    }
                }
                if(!nonce.empty())s.inventory=server_consumables(kind,nonce);
                if(s.inventory){s.apply_inventory(*s.inventory);nonce.clear();}
                else std::fputs("Consumable purchase confirmation unavailable; retry retains its nonce\n",stderr);
            }
        }
        abi::invoke(s.save->original(),{self});
    }
    static void __cdecl coins_entry(uint32_t self,uint32_t delta) {
        auto& s=*active;
        if(shopping && reconciled)return;
        abi::invoke(s.coins->original(),{self,delta});
    }
    static void __cdecl activate_entry(uint32_t self,uint32_t type) {
        auto& s=*active;
        if(!s.dedicated() || approved) { abi::invoke(s.activate->original(),{self,type});return; }
        if(type>=9 || read<int32_t>(self,type*4)<=0 || read<int32_t>(self,0x24+type*4)>0)return;
        const auto manager=read(s.manager_slot);
        if(!manager || read(manager,0x3a04)>=12)return;
        const auto now=GetTickCount();
        if(s.pending[type] && now-s.pending[type]<2000)return;
        s.pending[type]=now;
        // Always request authority first, including native local-only packs.
        abi::invoke(s.request->original(),{manager,read(manager,0x3a04),type});
    }
    static void __cdecl request_entry(uint32_t manager,uint32_t actor,uint32_t type) {
        auto& s=*active;
        if(!approved)abi::invoke(s.request->original(),{manager,actor,type});
    }
    static void __cdecl notify_entry(uint32_t manager,uint32_t actor,uint32_t type) {
        auto& s=*active;
        if(approved)return;
        abi::invoke(s.dedicated()?s.request->original():s.notify->original(),{manager,actor,type});
    }
    static void __cdecl use_entry(uint32_t self,uint32_t actor,uint32_t type) {
        auto& s=*active;const auto manager=read(s.manager_slot);
        if(!approved && s.dedicated() && manager && actor==read(manager,0x3a04) && type<9) {
            // Native162 is the server's approval. Spend/save and start the
            // original local effects once, suppressing its outgoing echo.
            struct Approval { Approval(){approved=true;} ~Approval(){approved=false;} } scope;
            abi::invoke(s.activate->original(),{self,type});
            abi::invoke(s.use->original(),{self,actor,type});
            s.pending[type]=0;
            const auto level=read(s.level_slot),normal=level?read(level,0x17c):0;
            if(normal)abi::invoke(s.update_frames,{normal,0});
            return;
        }
        abi::invoke(s.use->original(),{self,actor,type});
    }
};
NativeConsumables::Impl* NativeConsumables::Impl::active=nullptr;
thread_local bool NativeConsumables::Impl::approved=false;
thread_local bool NativeConsumables::Impl::shopping=false;
thread_local bool NativeConsumables::Impl::reconciled=false;
NativeConsumables::NativeConsumables(const ElfImage& image,bool (*mode)()):impl_(std::make_unique<Impl>(image,mode)){}
NativeConsumables::~NativeConsumables()=default;
}
