#include "abi/guest_hook.h"
#include "bionic/runtime.h"
#include "loader/diagnostics.h"
#include "loader/sha256.h"
#include "net/native_snapshot_presentation.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <intrin.h>

namespace {
void check(bool v, const char *m) { if (!v) throw std::runtime_error(m); }
uint32_t ptr(const void *v) { return reinterpret_cast<uint32_t>(v); }
uint32_t word(uint32_t v, size_t offset) { return *reinterpret_cast<uint32_t *>(v+offset); }
bool text(uint32_t v, const char *s) { return v && std::strcmp(reinterpret_cast<char *>(v),s)==0; }
uint32_t snapshotClock=217, snapshotLocal=0;
std::string capturedAccountRequest;
uint32_t completedAccountFunction=0;
uint32_t __cdecl capture_account_request(uint32_t,uint32_t body,uint32_t first,uint32_t second) {
    check(first==1 && second==1,"native account transport flags");
    capturedAccountRequest=reinterpret_cast<const char *>(body);return 1;
}
uint32_t __cdecl capture_account_success(uint32_t,uint32_t function) {
    completedAccountFunction=function;return 0;
}
std::vector<uint8_t> sceneMessage;
bool sceneConsumed=false;
uint32_t sceneRemote=0, sceneLocalPlayer=0, sceneRespawns=0, sceneWeapon=0;
uint32_t sceneLocalRespawns=0, sceneUnfreezes=0;
std::array<uint32_t,10> receivedLoadout{};
uint32_t receivedLoadouts=0;
uint32_t __cdecl scene_loadout(uint32_t,uint32_t source) {
    std::memcpy(receivedLoadout.data(),reinterpret_cast<void *>(source),40);++receivedLoadouts;return 0;
}
std::array<uint32_t,6> deathFields{};
uint32_t __cdecl scene_death(uint32_t,uint32_t victim,uint32_t attacker,uint32_t weapon,uint32_t head,uint32_t source,uint32_t part,uint32_t) {
    deathFields={victim,attacker,weapon,head,source,part};return 0;
}
uint32_t damageAttacker=0,damageTargetType=0;
uint32_t pickupComponent=0;
uint32_t __cdecl pickup_component(uint32_t,uint32_t kind) { return kind==120?pickupComponent:0; }
uint32_t __cdecl pickup_resolve(uint32_t,uint32_t index) { check(index==0,"native pickup child");return pickupComponent; }
uint32_t __cdecl damage_identity(uint32_t,uint32_t object,uint32_t id,uint32_t type) {
    *reinterpret_cast<uint32_t *>(id)=object==damageAttacker?0:1;
    *reinterpret_cast<uint32_t *>(type)=object==damageAttacker?0:damageTargetType;return 0;
}
float __cdecl damage_height(uint32_t) { return 2; }
uint32_t __cdecl damage_effect(uint32_t,uint32_t,float) { return 0x21; }
uint32_t __cdecl scene_queue(uint32_t,uint32_t size,uint32_t device) {
    if(sceneConsumed) return 0;
    sceneConsumed=true;
    *reinterpret_cast<uint16_t *>(size)=static_cast<uint16_t>(sceneMessage.size());
    *reinterpret_cast<uint8_t *>(device)=0;
    return ptr(sceneMessage.data());
}
uint32_t __cdecl scene_noop(uint32_t,...) { return 0; }
uint32_t __cdecl scene_component(uint32_t object,uint32_t kind) {
    if(kind==0x2c && object==word(sceneRemote,4)) return sceneRemote;
    if(kind==0x2a && object==word(sceneLocalPlayer,4)) return sceneLocalPlayer;
    return 0;
}
uint32_t __cdecl scene_local_respawn(uint32_t player) {
    check(player==sceneLocalPlayer,"local respawn receiver");++sceneLocalRespawns;return 0;
}
uint32_t __cdecl scene_unfreeze(uint32_t,uint32_t frozen) {
    check(frozen==0,"round start unfreezes actors");++sceneUnfreezes;return 0;
}
uint32_t __cdecl scene_respawn(uint32_t remote) {
    check(remote==sceneRemote,"scene remote receiver");
    const auto object=word(remote,4);
    check(*reinterpret_cast<uint8_t *>(object+0x8c)==1 && *reinterpret_cast<uint8_t *>(object+0x158)==1,
        "original scene activation enables and shows remote object before respawn");
    ++sceneRespawns;return 0;
}
uint32_t __cdecl scene_weapon(uint32_t remote,uint32_t weapon) {
    check(remote==sceneRemote,"scene weapon receiver");sceneWeapon=weapon;return 0;
}
uint32_t __cdecl scene_local_player(uint32_t) { return sceneLocalPlayer; }
uint32_t initialLevel=0, initialRemote=0;
uint32_t removedSlot=255;
uint32_t __cdecl capture_player_quit(uint32_t,uint32_t slot) { removedSlot=slot; return 0; }
uint32_t __cdecl initial_level() { return initialLevel; }
uint32_t __cdecl initial_component(uint32_t,uint32_t kind) { check(kind==0x2c,"initial remote component"); return initialRemote; }
uint32_t __cdecl initial_weapon(uint32_t) { return 23085; }
uint32_t __cdecl initial_score(uint32_t) { return 0x01020304; }
bool snapshotAligned=true;
uint32_t readyCalls=0, readyStatus=0;
uint32_t __cdecl capture_ready(uint32_t, uint32_t status) { ++readyCalls; readyStatus=status; return 1; }
uint32_t outgoingLobbyPacket=0;
uint32_t __cdecl capture_lobby_packet(uint32_t, uint32_t packet) { outgoingLobbyPacket=packet; return 1; }
uint32_t __cdecl ignore_retry_copy(uint32_t, uint32_t) { return 0; }
uint32_t nativeAckCount=0;
std::vector<uint8_t> nativePing;
std::vector<uint8_t> nativeChat;
bool nativeEnd=false;
uint32_t __cdecl capture_end(uint32_t manager) {
    const auto packet=word(manager,0x4404);
    nativeEnd=*reinterpret_cast<uint8_t *>(word(packet,4)+13)==38;
    return 0;
}
uint32_t __cdecl capture_chat(uint32_t, uint32_t bytes, uint32_t size, uint32_t device, uint32_t reliable, uint32_t, uint32_t) {
    check(size==20 && device==255 && reliable==1,"original chat queue arguments");
    nativeChat.assign(reinterpret_cast<uint8_t *>(bytes)+13,reinterpret_cast<uint8_t *>(bytes)+size);
    return 0;
}
uint32_t __cdecl capture_ping(uint32_t, uint32_t bytes, uint32_t size, uint32_t device, uint32_t reliable, uint32_t, uint32_t) {
    check(size==22 && device==0 && reliable==0,"original ping queue arguments");
    nativePing.assign(reinterpret_cast<uint8_t *>(bytes)+13,reinterpret_cast<uint8_t *>(bytes)+size);
    return 0;
}
uint32_t __cdecl drained_send_queue(uint32_t, uint32_t, uint32_t) { return 1; }
uint32_t __cdecl capture_ack(uint32_t, uint32_t bytes, uint32_t size, uint32_t, uint32_t, uint32_t, uint32_t) {
    if(size==14 && reinterpret_cast<const uint8_t *>(bytes)[13]==15) ++nativeAckCount;
    return 0;
}
std::vector<nova::net::pc::Snapshot> capturedSnapshots;
bool capture_snapshot(const nova::net::pc::Snapshot &s) { capturedSnapshots.push_back(s); return true; }
uint32_t __cdecl snapshot_clock() { return snapshotClock; }
uint32_t __cdecl snapshot_local(uint32_t) { return snapshotLocal; }
uint32_t __cdecl snapshot_rotation(uint32_t object,uint32_t angles) {
    snapshotAligned &= (reinterpret_cast<uintptr_t>(_AddressOfReturnAddress())+4)%16==0;
    std::memcpy(reinterpret_cast<void *>(object+0x60),reinterpret_cast<void *>(angles),12);
    return 0;
}
std::vector<uint8_t> unhex(const std::string &s) {
    std::vector<uint8_t> b;
    for (size_t i=0;i<s.size();i+=2) b.push_back(static_cast<uint8_t>(std::stoul(s.substr(i,2),nullptr,16)));
    return b;
}
}
int main(int argc,char **argv) {
    try {
        check(argc==2,"usage: native-room-codec-tests ELF");
        if (!std::getenv("NOVA_REAL_TESTS") || std::string(std::getenv("NOVA_REAL_TESTS"))!="1" || !std::filesystem::is_regular_file(argv[1])) return 77;
        nova::ImportRegistry imports; nova::bionic::register_boot(imports);
        nova::bionic::CxaRegistry cxa(imports); nova::ElfImage image(argv[1],imports);
        check(nova::sha256(image.source_bytes())=="a672cf14e27f1c188edd97d85d220becd99e641ad9cb552987512d5ef90e102e","native identity");
        nova::Diagnostics diagnostics(image); image.run_initializers();
        auto function=[&](const char *s){return nova::abi::require_function(image,s);};
        {
            // Presets and player beacons retain one-based selections. Execute
            // the actual resolver rather than assuming catalogue ordinals.
            std::array<uint32_t,0xb144/4> manager{};
            std::array<uint32_t,0x64/4> component{};
            std::array<uint32_t,10> selection{};
            std::array<uint32_t,4> primaryTable{},secondaryTable{};
            std::array<uint32_t,48> primaryRows{},secondaryRows{};
            primaryTable[3]=ptr(primaryRows.data());secondaryTable[3]=ptr(secondaryRows.data());
            manager[0xaf98/4]=ptr(primaryTable.data());manager[0xaf9c/4]=ptr(secondaryTable.data());
            for(unsigned i=0;i<3;++i) {primaryRows[16*i]=100+i;secondaryRows[16*i]=200+i;}
            auto *singleton=reinterpret_cast<uint32_t *>(image.symbol("_ZN13GameMpManager9SingletonE"));
            const auto previous=*singleton;*singleton=ptr(manager.data());
            for(unsigned index=0;index<=3;++index) {
                selection[0]=index;selection[1]=index;
                selection[9]=index%2;
                nova::abi::invoke(function("_ZN18MPLoadoutComponent10SetLoadoutER13PlayerLoadout"),{ptr(component.data()),ptr(selection.data())});
                check(nova::abi::invoke(function("_ZN18MPLoadoutComponent16GetPrimaryWeaponEv"),{ptr(component.data())})==(index?99+index:0xffffffff),"native one-based primary selection");
                check(nova::abi::invoke(function("_ZN18MPLoadoutComponent18GetSecondaryWeaponEv"),{ptr(component.data())})==(index?199+index:0xffffffff),"native one-based secondary selection");
                check(nova::abi::invoke(function("_ZN18MPLoadoutComponent10HasJetpackEv"),{ptr(component.data())})==index%2,"native loadout jetpack bool");
            }
            *singleton=previous;
        }
        {
            for(const char *value:{"public value","n",""}) {
                std::array<uint32_t,0x140/4> user{};
                const std::string body=std::string("n|alice|")+value+"|";
                nova::abi::invoke(function("_ZN13GLXPlayerUser15processUserDataEPc"),{ptr(user.data()),ptr(body.c_str())});
                check(text(nova::abi::invoke(function("_ZN13GLXPlayerUser11getUserDataEv"),{ptr(user.data())}),value),"native SDK public store value");
                nova::abi::invoke(function("_ZN13GLXPlayerUser15processUserDataEPc"),{ptr(user.data()),0});
                nova::abi::invoke(function("_ZdaPv"),{user[0xd4/4]});
            }
        }
        {
            std::array<uint32_t,0x140/4> user{};
            std::array<uint32_t,4> vtable{};
            vtable[3]=ptr(reinterpret_cast<void *>(&capture_account_request));
            user[0]=ptr(vtable.data());user[2]=28003;
            nova::abi::invoke(function("_ZN13GLXPlayerUser18sendChangeUserNameEPcS0_S0_b"),
                {ptr(user.data()),ptr("test-uid"),ptr("test-password"),ptr("new-name"),0});
            check(capturedAccountRequest=="f|14|i|28003|u|test-uid|p|test-password|nu|new-name|o|0|","native rename request layout");
            nova::abi::GuestCall complete(image,"_ZN13GLXPlayerUser15OnUpdateSuccessEi",0x10a,
                "_ZN21GLXPlayerWebComponent15OnUpdateSuccessEi",ptr(reinterpret_cast<void *>(&capture_account_success)));
            nova::abi::invoke(function("_ZN13GLXPlayerUser15OnUpdateSuccessEi"),{ptr(user.data()),14});
            check(completedAccountFunction==14,"native rename accepts success without a body");
        }
        {
            std::array<uint32_t,0xe0/4> user{};
            const std::string body="u|p1|username|alice|email|alice@example.invalid|email_flag|1|country|IT|reserved_1||reserved_2||reserved_3|0|firstname|Alice|lastname|Example|sex|1|birthday|2000-01-02|online_time|0|trophy|0|";
            const auto parse=function("_ZN13GLXPlayerUser22processUserInformationEPc");
            for(const auto &value:std::array<std::string,2>{body,"n|alice|"+body}) {
                nova::abi::invoke(parse,{ptr(user.data()),ptr(value.c_str())});
                auto get=[&](const char *name){return nova::abi::invoke(function(name),{ptr(user.data())});};
                check(text(get("_ZN13GLXPlayerUser11getUserNameEv"),"alice"),"original profile name");
                check(text(get("_ZN13GLXPlayerUser8getEmailEv"),"alice@example.invalid") && get("_ZN13GLXPlayerUser12getEmailFlagEv")==1,"original profile email");
                check(text(get("_ZN13GLXPlayerUser10getCountryEv"),"IT") && text(get("_ZN13GLXPlayerUser12getFirstNameEv"),"Alice"),"original profile country/first name");
                check(text(get("_ZN13GLXPlayerUser11getLastNameEv"),"Example") && get("_ZN13GLXPlayerUser6getSexEv")==1,"original profile last name/sex");
                check(text(get("_ZN13GLXPlayerUser11getBirthdayEv"),"2000-01-02"),"original profile birthday");
                check(get("_ZN13GLXPlayerUser11getPasswordEv")==0,"profile never returns password");
            }
            nova::abi::invoke(function("_ZN13GLXPlayerUser20clearUserInformationEv"),{ptr(user.data())});
        }
        {
            std::array<uint32_t,0xe0/4> user{};
            for(const char *state:{"n|alice|t|3|g|28003|a|28003|","un|27|t|0|g|28003|a|28003|"}) {
                nova::abi::invoke(function("_ZN13GLXPlayerUser16processUserStateEPc"),{ptr(user.data()),ptr(state)});
                check(user[0x8c/4]==28003 && text(user[0x94/4],"28003"),"original public state application identity");
                check(nova::abi::invoke(function("_ZN13GLXPlayerUser19getCurrentUserStateEv"),{ptr(user.data())})==(state[0]=='n'?3:0),"original public presence value");
            }
            const char empty[]="";
            nova::abi::invoke(function("_ZN13GLXPlayerUser16processUserStateEPc"),{ptr(user.data()),ptr(empty)});
        }
        {
            alignas(16) std::array<uint32_t,0xc0/4> messages{};
            const auto object=ptr(messages.data());
            const char counts[]="1^2^3^5|2^4^5^9|4^6^7^13|8^8^9^17|16^10^11^21|32^30^35^65|";
            nova::abi::invoke(function("_ZN16GLXPlayerMessage15processMsgCountEPc"),{object,ptr(counts)});
            check(nova::abi::invoke(function("_ZN16GLXPlayerMessage11getMsgCountEii"),{object,1,0})==2,"original unread count");
            check(nova::abi::invoke(function("_ZN16GLXPlayerMessage11getMsgCountEii"),{object,16,1})==11,"original read count");
            check(nova::abi::invoke(function("_ZN16GLXPlayerMessage11getMsgCountEii"),{object,32,2})==65,"original total count");
            const char inbox[]="c|1|m|27^4^Join my room^alice^42^2026-11-07 01:30:00^0|";
            nova::abi::invoke(function("_ZN16GLXPlayerMessage20processMsgHeaderListEPc"),{object,ptr(inbox)});
            auto get=[&](const char *name){return nova::abi::invoke(function(name),{object,0});};
            check(get("_ZN16GLXPlayerMessage15getMsgListCountEv")==1 && get("_ZN16GLXPlayerMessage8getMsgIDEi")==27,"original inbox identity");
            check(get("_ZN16GLXPlayerMessage10getMsgTypeEi")==4 && get("_ZN16GLXPlayerMessage15getMsgReadStateEi")==0,"original inbox type/state");
            check(text(get("_ZN16GLXPlayerMessage11getMsgTitleEi"),"Join my room"),"original message title");
            check(text(get("_ZN16GLXPlayerMessage12getMsgSenderEi"),"alice") && text(get("_ZN16GLXPlayerMessage18getMsgSenderNumberEi"),"42"),"original message sender");
            check(text(get("_ZN16GLXPlayerMessage17getMsgCreatedTimeEi"),"2026-11-07 01:30:00"),"original message date");
            const char sent[]="c|1|m|28^Hello^43^bob^2026-11-07 01:31:00|";
            nova::abi::invoke(function("_ZN16GLXPlayerMessage26processMySentMsgHeaderListEPc"),{object,ptr(sent)});
            check(get("_ZN16GLXPlayerMessage8getMsgIDEi")==28 && text(get("_ZN16GLXPlayerMessage11getMsgTitleEi"),"Hello"),"original sent header");
            check(text(get("_ZN16GLXPlayerMessage23getMsgDesUserNumberListEi"),"43") && text(get("_ZN16GLXPlayerMessage21getMsgDesUserNameListEi"),"bob"),"original sent recipient");
            nova::abi::invoke(function("_ZN16GLXPlayerMessage16clearMessageListEv"),{object});
        }
        {
            // Execute the original nonempty friends reader with the server's
            // twelve-field layout. Distinct values catch field-order mistakes.
            std::array<uint32_t,0x80/4> friends{};
            const char list[]="c|1|f|27^alice^IT^1.5,2,3^41^3^28003^7^en^local-default^123^1|";
            nova::abi::invoke(function("_ZN19GLXPlayerUserFriend18processFriendsListEPc"),{ptr(friends.data()),ptr(list)});
            auto get=[&](const char *symbol){return nova::abi::invoke(function(symbol),{ptr(friends.data()),0});};
            check(get("_ZN19GLXPlayerUserFriend15GetFriendsCountEv")==1,"native friends count");
            check(text(get("_ZN19GLXPlayerUserFriend13GetUserNumberEi"),"27"),"native friend number");
            check(text(get("_ZN19GLXPlayerUserFriend7GetNameEi"),"alice"),"native friend name");
            check(text(get("_ZN19GLXPlayerUserFriend10GetCountryEi"),"IT"),"native friend country");
            check(get("_ZN19GLXPlayerUserFriend8GetStateEi")==3,"native friend presence");
            check(get("_ZN19GLXPlayerUserFriend19GetTotalTrophyScoreEi")==41,"native trophy total");
            check(get("_ZN19GLXPlayerUserFriend9GetTrophyEi")==7,"native trophies");
            check(text(get("_ZN19GLXPlayerUserFriend11GetLanguageEi"),"en"),"native language");
            check(text(get("_ZN19GLXPlayerUserFriend12GetAvatarKeyEi"),"local-default"),"native avatar");
            check(get("_ZN19GLXPlayerUserFriend9HasPlayedEi")==1,"native played flag");
            nova::abi::invoke(function("_ZN19GLXPlayerUserFriend16clearFriendsListEv"),{ptr(friends.data())});
        }
        {
            // Real ranking readers, including self prefix, eight-field rows,
            // around-me rows without a prefix and the nine-field detail reply.
            std::array<uint32_t,0x110/4> rankings{};
            const char top[]="y|2|0|3|4|5|6|7|8|9|10|11|12|13|14|0||0||0|0|1|alice|31|41|51|61|71|81|";
            nova::abi::invoke(function("_ZN18XPlayerLeaderBoard14processRankingEPc"),{ptr(rankings.data()),ptr(top)});
            check(rankings[0x44/4]==1 && rankings[0x70/4]==2,"original ranking self and row count");
            check(rankings[0x88/4]==6 && rankings[0x9c/4]==14,"original self XP and resets");
            check(rankings[0x80/4]==4 && rankings[0x84/4]==5,"original self kills and deaths");
            check(text(word(rankings[0x48/4],0),"alice"),"original ranking player name");
            for(auto [offset,value]:std::array<std::pair<size_t,uint32_t>,7>{{{0x50,1},{0x58,31},{0x5c,41},{0x60,51},{0x64,61},{0x54,71},{0x68,81}}})
                check(word(rankings[offset/4],0)==value,"original ranking row field order");
            nova::abi::invoke(function("_ZN18XPlayerLeaderBoard16clearLeaderboardEv"),{ptr(rankings.data())});
            const char around[]="4|bob|32|42|52|62|72|82|";
            nova::abi::invoke(function("_ZN18XPlayerLeaderBoard20processRankingAroundEPc"),{ptr(rankings.data()),ptr(around)});
            check(rankings[0x44/4]==1 && text(word(rankings[0x48/4],0),"bob") && word(rankings[0x50/4],0)==4,"original around-me response has no prefix");
            nova::abi::invoke(function("_ZN18XPlayerLeaderBoard16clearLeaderboardEv"),{ptr(rankings.data())});
            const char detail[]="1|2|3|4|5|6|7|8|9|";
            nova::abi::invoke(function("_ZN18XPlayerLeaderBoard20processRankingPlayerEPc"),{ptr(rankings.data()),ptr(detail)});
            for(size_t i=0;i<9;++i) check(rankings[0xc8/4+i]==i+1,"original ranking detail field order");
        }
        // The original builder receives GGI and login "un" separately and
        // serializes the latter. Earlier synthetic fixtures confused the two.
        {
            const char *builder="_ZN15ConnectionLobby23sendGetLobbyListPackageEiiihP23CLobbyParameterAndQueryh12LobbyVersionb";
            nova::abi::GuestCall retry(image,builder,0x11b,"_ZN15ConnectionLobby13saveRetryDataEP10DataPacket",ptr(reinterpret_cast<void *>(&ignore_retry_copy)));
            nova::abi::GuestCall output(image,builder,0x12a,"_ZN10Connection17addOutgoingPacketEP10DataPacket",ptr(reinterpret_cast<void *>(&capture_lobby_packet)));
            nova::abi::invoke(function(builder),{0,28003,1,0,50,0,1,2,1});
            check(outgoingLobbyPacket!=0,"original room query captured");
            // Finalized frame includes the 8-byte length/version/opcode header.
            const auto expected=unhex("0012010200000080000000013200000000000101");
            const auto size=nova::abi::invoke(function("_ZN15DataPacketLobby13getMessageLenEv"),{outgoingLobbyPacket});
            const auto body=nova::abi::invoke(function("_ZN15DataPacketLobby14getMessageBodyEv"),{outgoingLobbyPacket});
            check(size==expected.size() && std::memcmp(reinterpret_cast<void *>(body),expected.data(),size)==0,"room query contains user number, not GGI");
            nova::abi::invoke(function("_ZN15DataPacketLobbyD1Ev"),{outgoingLobbyPacket});
            nova::abi::invoke(function("_ZdlPv"),{outgoingLobbyPacket});
        }
        {
            const char *builder="_ZN15ConnectionLobby20sendJoinLobbyPackageEihPci";
            std::array<uint32_t,0x2060/4> connection{};
            nova::abi::GuestCall output(image,builder,0xf6,"_ZN10Connection17addOutgoingPacketEP10DataPacket",ptr(reinterpret_cast<void *>(&capture_lobby_packet)));
            const char data[]="x";
            nova::abi::invoke(function(builder),{ptr(connection.data()),7,3,ptr(data),1});
            const auto expected=unhex("000e01020000000f0000000703000178");
            const auto size=nova::abi::invoke(function("_ZN15DataPacketLobby13getMessageLenEv"),{outgoingLobbyPacket});
            const auto body=nova::abi::invoke(function("_ZN15DataPacketLobby14getMessageBodyEv"),{outgoingLobbyPacket});
            check(size==expected.size() && std::memcmp(reinterpret_cast<void *>(body),expected.data(),size)==0,"original join carries initial ready status 3");
            nova::abi::invoke(function("_ZN10Connection14cleanRetryDataEv"),{ptr(connection.data())});
            nova::abi::invoke(function("_ZN15DataPacketLobbyD1Ev"),{outgoingLobbyPacket});
            nova::abi::invoke(function("_ZdlPv"),{outgoingLobbyPacket});
        }
        {
            const char *builder="_ZN15ConnectionLobby19sendGetLobbyForNameEPc12LobbyVersionb";
            nova::abi::GuestCall retry(image,builder,0xce,"_ZN15ConnectionLobby13saveRetryDataEP10DataPacket",ptr(reinterpret_cast<void *>(&ignore_retry_copy)));
            nova::abi::GuestCall output(image,builder,0xdd,"_ZN10Connection17addOutgoingPacketEP10DataPacket",ptr(reinterpret_cast<void *>(&capture_lobby_packet)));
            const char name[]="alice";
            for (uint32_t version : {1u,2u}) {
                nova::abi::invoke(function(builder),{0,ptr(name),version,1});
                const auto expected=unhex(version==1 ? "00160102000000610000000000000000000005616c696365" : "00160102000000820000000000000000000005616c696365");
                const auto size=nova::abi::invoke(function("_ZN15DataPacketLobby13getMessageLenEv"),{outgoingLobbyPacket});
                const auto body=nova::abi::invoke(function("_ZN15DataPacketLobby14getMessageBodyEv"),{outgoingLobbyPacket});
                check(size==expected.size() && std::memcmp(reinterpret_cast<void *>(body),expected.data(),size)==0,"original name search versions");
                nova::abi::invoke(function("_ZN15DataPacketLobbyD1Ev"),{outgoingLobbyPacket});
                nova::abi::invoke(function("_ZdlPv"),{outgoingLobbyPacket});
            }
        }
        {
            const char *builder="_ZN15ConnectionLobby21sendGetLobbyForFriendEihhPciP23CLobbyParameterAndQueryh12LobbyVersionb";
            nova::abi::GuestCall retry(image,builder,0x182,"_ZN15ConnectionLobby13saveRetryDataEP10DataPacket",ptr(reinterpret_cast<void *>(&ignore_retry_copy)));
            nova::abi::GuestCall output(image,builder,0x191,"_ZN10Connection17addOutgoingPacketEP10DataPacket",ptr(reinterpret_cast<void *>(&capture_lobby_packet)));
            const auto names=unhex("0005616c6963650003626f62");
            nova::abi::invoke(function(builder),{0,23,1,2,ptr(names.data()),static_cast<uint32_t>(names.size()),0,1,2,1});
            const auto expected=unhex("001f01020000008400000017020000000001020005616c6963650003626f620001");
            const auto size=nova::abi::invoke(function("_ZN15DataPacketLobby13getMessageLenEv"),{outgoingLobbyPacket});
            const auto body=nova::abi::invoke(function("_ZN15DataPacketLobby14getMessageBodyEv"),{outgoingLobbyPacket});
            check(size==expected.size() && std::memcmp(reinterpret_cast<void *>(body),expected.data(),size)==0,"original friend search names and query tail");
            nova::abi::invoke(function("_ZN15DataPacketLobbyD1Ev"),{outgoingLobbyPacket});
            nova::abi::invoke(function("_ZdlPv"),{outgoingLobbyPacket});
        }
        std::array<uint32_t,9> packet{};
        nova::abi::invoke(function("_ZN15DataPacketLobbyC1Ev"),{ptr(packet.data())});
        auto load=[&](const std::string &hex) {
            const auto b=unhex(hex);
            nova::abi::invoke(function("_ZN15DataPacketLobby14setMessageBodyEPhi"),{ptr(packet.data()),ptr(b.data()),static_cast<uint32_t>(b.size())});
        };
        // Minimal receiver fixture: use the original readers and allocators,
        // with room/player arrays owned by the test. No game UI or socket runs.
        std::array<uint32_t,0x244/4> skin{};
        std::array<uint8_t,0x90> lobbyClient{};
        skin[0x2c/4]=ptr(lobbyClient.data());
        std::array<uint32_t,50> roomSlots{};
        std::array<uint32_t,12> playerSlots{};
        skin[0x10/4]=6;skin[0x158/4]=ptr(roomSlots.data());skin[0x16c/4]=ptr(playerSlots.data());
        std::memcpy(reinterpret_cast<char *>(skin.data())+0xb0,"p2",3);
        skin[0x180/4]=skin[0x184/4]=ptr(skin.data())+0x180;
        // Same fabricated golden bytes as the Go codec test, after dispatcher
        // consumes the success status. Distinct markers detect field swaps.
        load("00000001000000070002703100014102010100000000090000000a0001670000006b");
        nova::abi::invoke(function("_ZN10CLobbySkin22OnMPListSessionSuccessEP10DataPacket12LobbyVersion"),{ptr(skin.data()),ptr(packet.data()),2});
        check(skin[0x160/4]==1 && roomSlots[0],"native room count");
        const auto room=roomSlots[0];
        check(word(room,0)==7 && text(word(room,4),"p1") && text(word(room,8),"A"),"native room identity");
        const auto *raw=reinterpret_cast<const uint8_t *>(room);
        check(raw[0xe]==2 && raw[0x1c]==1 && raw[0xc]==1 && raw[0xd]==0,"native room flags/counts");
        check(word(room,0x14)==9 && word(room,0x18)==10 && word(room,0x10)==107 && text(word(room,0x20),"g"),"native room metadata");
        check(packet[3]==packet[4],"native list consumed exactly");
        skin[0x10/4]=11;skin[0x15c/4]=room;
        load("000270310001670100000000090000000a00014102010000006b00000000");
        nova::abi::invoke(function("_ZN10CLobbySkin16OnMPGetLobbyInfoEP10DataPacket"),{ptr(skin.data()),ptr(packet.data())});
        check(packet[3]==packet[4] && skin[0x18/4]==2,"room info includes trailing player count");
        // Execute the complete Join callback after the dispatcher reads status.
        skin[0x10/4]=12;
        load("0000000200027031000141030001780002703200014203000179");
        nova::abi::invoke(function("_ZN10CLobbySkin22OnMPJoinSessionSuccessEP10DataPacket"),{ptr(skin.data()),ptr(packet.data())});
        check(skin[0x174/4]==2 && skin[0x170/4]==playerSlots[1],"native local player selected by distinct ID");
        check(text(word(playerSlots[0],0),"p1") && text(word(playerSlots[1],0),"p2") && text(word(playerSlots[1],0xc),"y"),"native player records");
        check(packet[3]==packet[4],"native player records consumed exactly");
        // Rejoin has its own layout. The byte at room+0x1c is the started
        // flag, whereas QuickPlay/UpdateHostList use room+0x14 for occupancy.
        skin[0x10/4]=21;
        load("00027031010002000167000000000000000000000200027031000141030001780002703200014203000179");
        nova::abi::invoke(function("_ZN10CLobbySkin22OnMPRejoinLobbySuccessEP10DataPacketb"),{ptr(skin.data()),ptr(packet.data()),0});
        check(packet[3]==packet[4] && skin[0x174/4]==2 && skin[0x10/4]==13,"original waiting-room rejoin");
        check(reinterpret_cast<const uint8_t *>(room)[0x1c]==0 && text(word(room,0x20),"g"),"rejoin does not start a scene");
        // Execute the original readiness transition: opcode 39 sends 3, and
        // success opcode 40 commits it locally so the next update sends nothing.
        {
            nova::abi::GuestCall ready(image,"_ZN10CLobbySkin21IsHandleEventSetReadyEv",0x3d,
                "_ZN16GLXPlayerMPLobby21mpSendSetPlayerStatusEh",ptr(reinterpret_cast<void *>(&capture_ready)));
            auto *status=reinterpret_cast<uint8_t *>(playerSlots[1])+8;
            check(*status==3,"joined player retains requested ready status");
            nova::abi::invoke(function("_ZN10CLobbySkin21IsHandleEventSetReadyEv"),{ptr(skin.data())});
            check(readyCalls==0,"ready joiner does not resend");
            *status=2; // The Create callback starts the owner in this state.
            skin[0x10/4]=16;
            nova::abi::invoke(function("_ZN10CLobbySkin21IsHandleEventSetReadyEv"),{ptr(skin.data())});
            check(readyCalls==1 && readyStatus==3 && *status==2,"native ready request is 3");
            nova::abi::invoke(function("_ZN10CLobbySkin19OnMPSetParamSuccessEi"),{ptr(skin.data()),40});
            check(*status==3 && skin[0x18/4]==2,"native ready success commits state");
            nova::abi::invoke(function("_ZN10CLobbySkin21IsHandleEventSetReadyEv"),{ptr(skin.data())});
            check(readyCalls==1,"acknowledged ready does not retry");
        }
        // Original opcode 50 receiver, with the Go encoder's synthetic golden.
        // This proves field/state consumption, not host readiness or gameplay.
        const auto endpointPush=function("_ZN10CLobbySkin23OnMPProcessPushMessagesEP10DataPacketi");
        load("0002703200027031"); // Remaining owner p2, departing creator p1.
        nova::abi::invoke(endpointPush,{ptr(skin.data()),ptr(packet.data()),21});
        check(packet[3]==packet[4] && text(word(room,4),"p2"),"native owner transfer after departure");
        check(skin[0x174/4]==1 && text(word(playerSlots[0],0),"p2"),"native survivor roster");
        std::array<uint32_t,1> currentRoom{7};
        for (int scenario=0;scenario<3;++scenario) {
            skin[0x15c/4]=scenario==2 ? 0 : ptr(currentRoom.data());
            currentRoom[0]=scenario==1 ? 8U : 7U;
            skin[0x234/4]=99;
            skin[0x150/4]=0;
            load("00093132372e312e322e333039d43100000007");
            nova::abi::invoke(endpointPush,{ptr(skin.data()),ptr(packet.data()),50});
            check(text(ptr(skin.data())+0x1ac,"127.1.2.3"),"native game endpoint address");
            check((skin[0x22c/4]&0xffff)==12345 && (skin[0x22c/4]>>16)==54321,"native game endpoint port order");
            check(packet[3]==packet[4] && skin[0x48/4]==50,"native endpoint consumed exactly");
            if(scenario==0) {
                check(skin[0x10/4]==20 && skin[0x14/4]==3 && skin[0x18/4]==0 && skin[0x234/4]==0,"native matching-room handoff");
            } else {
                check(skin[0x150/4]==0x4080 && skin[0x10/4]==12 && skin[0x14/4]==3 && skin[0x18/4]==3,"native wrong/missing-room rejection");
            }
        }
        // Original UDP builders share fabricated goldens with the independent
        // Go codec. Nothing from the ELF is compiled into the server.
        const auto udpCtor=function("_ZN9UDPPacketC1Ei");
        const auto addType=function("_ZN17GenericDataPacket18addGameMessageTypeEh");
        const auto addInt=function("_ZN17GenericDataPacket6addIntEi");
        const auto addByte=function("_ZN17GenericDataPacket7addByteEh");
        const auto addBytes=function("_ZN17GenericDataPacket8addBytesEPhi");
        const auto addBlob=function("_ZN17GenericDataPacket15addBytesWithLenEPhi");
        std::array<uint32_t,9> udp{};
        auto integer=[&](uint32_t v){nova::abi::invoke(addInt,{ptr(udp.data()),v});};
        auto byte=[&](uint32_t v){nova::abi::invoke(addByte,{ptr(udp.data()),v});};
        auto blob=[&](const auto &v){nova::abi::invoke(addBlob,{ptr(udp.data()),ptr(v.data()),static_cast<uint32_t>(v.size())});};
        auto name=[&](const char *v){
            std::array<char,20> b{};std::strncpy(b.data(),v,b.size()-1);
            nova::abi::invoke(addBytes,{ptr(udp.data()),ptr(b.data()),static_cast<uint32_t>(b.size())});
        };
        auto begin=[&](uint32_t kind){
            udp.fill(0);
            nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
            nova::abi::invoke(addType,{ptr(udp.data()),kind});
        };
        auto finish=[&](const char *hex){
            nova::abi::invoke(function("_ZN17GenericDataPacket11packMessageEv"),{ptr(udp.data())});
            const auto expected=unhex(hex);
            check(udp[3]==13+expected.size(),"original UDP message length");
            check(std::memcmp(reinterpret_cast<void *>(udp[1]+13),expected.data(),expected.size())==0,"original UDP payload golden");
            nova::abi::invoke(function("_ZN17GenericDataPacketD2Ev"),{ptr(udp.data())});
        };
        const std::array<uint8_t,4> serverBeacon{0,1,2,0};
        const std::array<uint8_t,16> hostBeacon{};
        const std::array<uint8_t,16> peerBeacon{0,0x11,0x22,0x33};
        begin(1);integer(0x0ab25828);finish("010ab25828");
        begin(2);integer(0x0ab25828);blob(serverBeacon);name("R06Host");
        finish("0200200ab258280400010200523036486f737400000000000000000000000000");
        begin(3);integer(0x0ab25828);blob(peerBeacon);name("R06Peer");
        finish("03002c0ab2582810001122330000000000000000000000005230365065657200000000000000000000000000");
        begin(5);integer(1);blob(serverBeacon);byte(2);
        name("R06Host");blob(hostBeacon);byte(0);name("R06Peer");blob(peerBeacon);byte(1);
        finish("05005900000001040001020002523036486f7374000000000000000000000000001000000000000000000000000000000000005230365065657200000000000000000000000000100011223300000000000000000000000001");
        begin(16);byte(1);byte(0);integer(300);integer(600);byte(0);byte(2);
        for(auto flag:{1,0,1,0,1}) byte(flag);
        finish("1001000000012c0000025800020100010001");
        begin(17);finish("11");begin(15);finish("0f");
        begin(6);byte(49);finish("0631");
        begin(7);byte(49);integer(0xfffffff0);finish("0731fffffff0");
        begin(8);integer(2);integer(static_cast<uint32_t>(-1000));finish("0800000002fffffc18");
        begin(13);integer(0x01020304);integer(0);finish("0d0102030400000000");
        begin(18);integer(2000);integer(0x11223344);integer(0x12345678);integer(0x01020304);
        finish("12000007d0112233441234567801020304");
        {
            std::array<uint32_t,0xb144/4> manager{};
            std::array<uint32_t,0x10c/4> player{};
            std::array<uint32_t,2> details{0,11};
            std::array<uint32_t,1> entries{ptr(details.data())};
            player[1]=0x1234;sceneLocalPlayer=ptr(player.data());
            manager[0x3bdc/4]=ptr(entries.data());manager[0x4404/4]=ptr(udp.data());
            unsigned value=1;
            for(auto offset:{0x3bcc,0x3bd0,0x3bb4,0x3bbc,0x3bc0,0x3bb8,0x3bc4,0x3bc8,0x3bd4}) manager[offset/4]=value++;
            manager[0x3bd8/4]=1;
            nova::abi::GuestHook component(image,"_ZNK11CGameObject12GetComponentEi",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&scene_component)));
            udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
            nova::abi::invoke(function("_ZN13GameMpManager26MP_ClientAddLoadoutMessageEP11CGameObject"),{ptr(manager.data()),0x1234});
            finish("210b01020304050607080901");
            sceneLocalPlayer=0;
        }
        begin(46);finish("2e");
        {
            std::array<uint32_t,0xb144/4> manager{};
            manager[0x4404/4]=ptr(udp.data());
            manager[0xa43c/4]=252;
            udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
            nova::abi::invoke(function("_ZN13GameMpManager23MP_AddChangeZoneMessageEiii"),
                              {ptr(manager.data()),2,2,0x01020304});
            finish("6b020201020304fd");
            udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
            nova::abi::invoke(function("_ZN13GameMpManager23MP_AddChangeZoneMessageEiii"),
                              {ptr(manager.data()),2,2,0x01020304});
            finish("6b02020102030400");
        }
        {
            std::array<uint32_t,0xb144/4> mp{};
            std::array<uint32_t,0x160/4> object{};
            std::array<uint32_t,0xb4/4> zone{};
            std::array<uint32_t,0x20/4> component{}, definition{};
            pickupComponent=ptr(component.data());component[0x10/4]=ptr(definition.data());
            definition[1]=0x1234;definition[2]=0x5678;definition[4]=90;component[0x1c/4]=77;
            mp[0x4404/4]=ptr(udp.data());mp[0xa40c/4]=ptr(udp.data());mp[0x3a04/4]=99;
            auto *roster=reinterpret_cast<uint8_t *>(mp.data())+0x3794;roster[0]=1;roster[1]=1;
            object[0x50/4]=0x01020304;object[0xe8/4]=ptr(zone.data());zone[0xb0/4]=0x01020305;
            reinterpret_cast<uint8_t *>(object.data())[0x158]=1;
            const std::array<float,3> position{10,20,30};std::memcpy(object.data()+0x54/4,position.data(),12);
            mp[0xaf30/4]=0x41200000;mp[0xaf38/4]=0x41a00000;mp[0xaf40/4]=0x41f00000;
            nova::abi::GuestHook lookup(image,"_ZNK11CGameObject12GetComponentEi",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&pickup_component)));
            nova::abi::GuestHook resolve(image,"_ZN17CPowerUpComponent29GetPowerupComponentFromObjectERP11CGameObjecti",{0x55,0x89,0xe5,0x83,0xec,0x28},ptr(reinterpret_cast<void *>(&pickup_resolve)));
            auto begin=[&](){udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});};
            begin();nova::abi::invoke(function("_ZN13GameMpManager33MP_ServerAddConsumePowerUpMessageEiii"),{ptr(mp.data()),2,0x01020304,3});
            finish("25020102030403");
            begin();nova::abi::invoke(function("_ZN13GameMpManager28MP_ServerAddMpPowerUpMessageEP11CGameObjecti"),{ptr(mp.data()),ptr(object.data()),0xffffffff});
            finish("280102030401");
            for(unsigned flag=0;flag<2;++flag) {
                component[0x18/4]=flag;begin();
                nova::abi::invoke(function("_ZN13GameMpManager26MP_ServerAddPowerUpMessageEP11CGameObjecti"),{ptr(mp.data()),ptr(object.data()),0xffffffff});
                finish(flag?"2701020304034d12345a56780064019003840102030500":"2701020304010012345a56780064019003840102030500");
            }
        }
        {
            // Execute the complete original 781-byte initial snapshot builder.
            // Only scene lookups are supplied; serialization runs unchanged.
            const char *builder="_ZN13GameMpManager30MP_ServerAddPlayersSnapshotLowEi";
            std::array<uint32_t,0xb144/4> mp{};
            std::array<uint32_t,0x800/4> level{};
            std::array<uint32_t,0xa0/4> remote{};
            std::array<std::array<uint32_t,0x1b0/4>,12> players{};
            std::array<std::array<uint32_t,0x100/4>,12> objects{};
            std::array<uint32_t,12> playerPointers{};
            initialLevel=ptr(level.data());initialRemote=ptr(remote.data());
            mp[0x3a04/4]=99;mp[0x3bdc/4]=ptr(playerPointers.data());mp[0xa40c/4]=ptr(udp.data());
            for(unsigned i=0;i<12;++i) {
                auto *row=reinterpret_cast<uint8_t *>(mp.data())+0x3794+i*0x34;
                row[0]=1;row[1]=i<2?1:0;row[0x16]=static_cast<uint8_t>(i);row[0x18]=static_cast<uint8_t>(i);
                std::memcpy(row+2,"R06Peer",7);
                for(unsigned j=0;j<16;++j) row[0x19+j]=static_cast<uint8_t>(0x10+j);
                playerPointers[i]=ptr(players[i].data());players[i][0]=ptr(objects[i].data());
                players[i][0x5c/4]=1;players[i][0x6c/4]=2;players[i][0x14/4]=3;
                players[i][0x60/4]=4;players[i][0x74/4]=5;players[i][0x80/4]=6;
                reinterpret_cast<uint8_t *>(players[i].data())[0x24]=1;
                reinterpret_cast<uint8_t *>(players[i].data())[0xb4]=1;
            }
            players[0][0x124/4]=0x11223344;
            auto *logCell=reinterpret_cast<uint32_t *>(image.symbol("_ZN10NetworkLog9SingletonE"));
            const auto oldLog=*logCell;uint32_t silentLog=0x7fffffff;*logCell=ptr(&silentLog);
            nova::abi::GuestCall levelA(image,builder,0x17d,"_ZN6CLevel8GetLevelEv",ptr(reinterpret_cast<void *>(&initial_level)));
            nova::abi::GuestCall levelB(image,builder,0x195,"_ZN6CLevel8GetLevelEv",ptr(reinterpret_cast<void *>(&initial_level)));
            nova::abi::GuestHook component(image,"_ZNK11CGameObject12GetComponentEi",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&initial_component)));
            nova::abi::GuestHook weapon(image,"_ZNK21RemotePlayerComponent20GetCurrentWeaponTypeEv",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&initial_weapon)));
            nova::abi::GuestHook score(image,"_ZN8MpPlayer14GetPlayerScoreEv",{0x55,0x89,0xe5,0x8b,0x45,0x08},ptr(reinterpret_cast<void *>(&initial_score)));
            udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
            nova::abi::invoke(function(builder),{ptr(mp.data()),0});
            nova::abi::invoke(function("_ZN17GenericDataPacket11packMessageEv"),{ptr(udp.data())});
            check(udp[3]==13+781,"original initial snapshot length");
            auto *body=reinterpret_cast<const uint8_t *>(udp[1]+13);
            check(body[0]==56,"original initial snapshot type");
            for(unsigned i=1;i<21;++i) check(body[i]==0,"FFA initial objective state");
            check(std::memcmp(body+21,unhex("11223344").data(),4)==0,"initial recipient render delay");
            for(unsigned i=0;i<12;++i) {
                auto *row=body+25+i*63;
                check(row[0]==1 && row[1]==(i<2?1:0) && row[2]==i && row[3]==i,"initial occupied/active/device/slot order");
                for(unsigned j=0;j<16;++j) check(row[4+j]==0x10+j,"initial beacon order");
                check(std::memcmp(row+20,"R06Peer",8)==0,"initial name order");
                const auto tail=unhex("00010002000301020304000400050006035a2d00000000");
                check(std::memcmp(row+40,tail.data(),tail.size())==0,"initial counters/flags/weapon/mount order");
            }
            nova::abi::invoke(function("_ZN17GenericDataPacketD2Ev"),{ptr(udp.data())});
            *logCell=oldLog;
        }
        {
            // Original complete type-19 builder: the announced actor is not
            // the recipient. This message activates the remote scene object.
            std::array<uint8_t,0xb144> mp{};
            mp[0x3794]=7; mp[0x37aa]=3; mp[0x37ac]=1;
            std::memcpy(mp.data()+0x3796,"R06Peer",7);
            for(unsigned i=0;i<16;++i) mp[0x37ad+i]=static_cast<uint8_t>(0x10+i);
            udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
            nova::abi::invoke(function("_ZN13GameMpManager28MP_ServerAddNewPlayerMessageEiiP9UDPPacket"),
                              {ptr(mp.data()),1,0x01020304,ptr(udp.data())});
            finish("13010703101112131415161718191a1b1c1d1e1f523036506565720000000000000000000000000001020304");
        }
        {
            // Execute the real launch-bullet builder, including quantization,
            // weapon lookup, native actor lookup and both optional fields.
            // A fabricated empty component list selects the manager's actor
            // registry; no original component/game object is initialized.
            std::array<uint32_t,0xb144/4> gameManager{};
            std::array<uint32_t,0x100/4> actor{};
            std::array<uint32_t,3> actors{ptr(actor.data()),2,0};
            std::array<uint32_t,2> weapons{100,200};
            gameManager[0xb13c/4]=ptr(actors.data());gameManager[0xb140/4]=ptr(actors.data()+actors.size());
            gameManager[0xa470/4]=ptr(weapons.data());gameManager[0xa474/4]=ptr(weapons.data()+weapons.size());
            gameManager[0xaf30/4]=0x41200000; // 10, 20, 30 quantization factors
            gameManager[0xaf38/4]=0x41a00000;gameManager[0xaf40/4]=0x41f00000;
            const std::array<float,3> origin{10,20,30},target{13,20,30};
            const auto launch=function("_ZN13GameMpManager33MP_AddLaunchBulletMessageToPacketEP9UDPPacketRKN6glitch4core8vector3dIfEES7_iP11CGameObjectibbfbiii");
            for(uint32_t flags:{0U,1U}) {
                udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
                nova::abi::invoke(launch,{ptr(gameManager.data()),ptr(udp.data()),ptr(origin.data()),ptr(target.data()),200,ptr(actor.data()),
                    flags,flags,flags,0x40400000,flags,flags?1U:3U,7,0x01020304});
                finish(flags?"18001a0102030402006401900384ffff7fff7fff011f26660107":"1800160102030402006401900384ffff7fff7fff0100");
            }
        }
        {
            // Reuse the original command builders; reload is player state 3,
            // not the empty MP_AddReloadWeaponMessage function.
            std::array<uint32_t,0xb144/4> mp{};
            std::array<uint32_t,0x50/4> clientComms{};
            mp[1]=ptr(clientComms.data());mp[0x4404/4]=ptr(udp.data());
            auto action=[&](const char *symbol,uint32_t value,const char *golden) {
                udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
                nova::abi::invoke(function(symbol),{ptr(mp.data()),11,value,0xffffffff});
                finish(golden);
            };
            action("_ZN13GameMpManager25MP_AddChangeWeaponMessageEiii",0x01020304,"220b01020304");
            action("_ZN13GameMpManager21MP_AddFireStopMessageEiii",0xffffffff,"370bffffffff");
            action("_ZN13GameMpManager22MP_AddAimWeaponMessageEibi",0,"240b");
            action("_ZN13GameMpManager22MP_AddAimWeaponMessageEibi",1,"248b");
            action("_ZN13GameMpManager23MP_AddPlayerChangeStateEiii",3,"89b3");
        }
        {
            std::array<uint32_t,0xb144/4> mp{};
            std::array<uint32_t,2> grenades{10,20},weapons{100,200};
            mp[0xa47c/4]=ptr(grenades.data());mp[0xa480/4]=ptr(grenades.data()+2);
            mp[0xa470/4]=ptr(weapons.data());mp[0xa474/4]=ptr(weapons.data()+2);
            mp[0xaf30/4]=0x41200000;mp[0xaf38/4]=0x41a00000;mp[0xaf40/4]=0x41f00000;
            const std::array<float,3> origin{10,20,30},direction{1,0,0},movement{0,1,0};
            for(uint32_t speed:{0U,0x3fc00000U}) {
                udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
                nova::abi::invoke(function("_ZN13GameMpManager33MP_AddThrowGrenadeMessageToPacketEP9UDPPacketN6glitch4core8vector3dIfEES5_S5_iifiii"),
                    {ptr(mp.data()),ptr(udp.data()),ptr(origin.data()),ptr(direction.data()),ptr(movement.data()),20,200,speed,0xffffffff,0x01020304,2});
                finish(speed?"1b0201020304006401900384ffff7fff7fff7fffffff7fff01010000c03f":"1a0201020304006401900384ffff7fff7fff7fffffff7fff0101");
            }
            // Melee uses the most recent local position sample, not its vector
            // argument. The wire slot names the victim, not the local attacker.
            std::array<uint32_t,0x11c/4> player{};
            std::array<uint32_t,4> position{0x41200000,0x41a00000,0x41f00000,0x01020304};
            std::array<uint32_t,1> players{ptr(player.data())};
            player[0x114/4]=ptr(position.data());player[0x118/4]=ptr(position.data()+4);
            mp[0x3bdc/4]=ptr(players.data());mp[0x4404/4]=ptr(udp.data());
            udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
            nova::abi::invoke(function("_ZN13GameMpManager24MP_ClientAddMeleeMessageEiiRKN6glitch4core8vector3dIfEE"),
                {ptr(mp.data()),1,200,ptr(direction.data())});
            finish("3e010203040100640190038401");
        }
        {
            const char *builder="_ZN13GameMpManager23MP_AddSendInGameMessageEiii";
            nova::abi::GuestCall queue(image,builder,0xb4,"_ZN5Comms17PutMessageInQueueEPhjhbbb",ptr(reinterpret_cast<void *>(&capture_chat)));
            std::array<uint32_t,0x50/4> mp{},clientComms{};
            mp[1]=ptr(clientComms.data());
            nova::abi::invoke(function(builder),{ptr(mp.data()),11,0,0x01020304});
            check(nativeChat==unhex("82000b01020304"),"original global chat builder");
            nova::abi::invoke(function(builder),{ptr(mp.data()),2,1,0x05060708});
            check(nativeChat==unhex("82010205060708"),"original team chat builder");
            // The preset integer is a zero-based localization-map index. The
            // original lookup adds STR_ when the template name omits it.
            const std::array<const char *,2> names{"STR_test0","STR_test1"};
            std::array<uint32_t,8> strings{};
            strings[0x14/4]=ptr(names.data());strings[0x18/4]=ptr(names.data()+names.size());
            for(const char *name:{"test1","STR_test1"}) {
                check(nova::abi::invoke(function("_ZNK8CStrings19GetStringIdFromNameEPKc"),
                    {ptr(strings.data()),ptr(name)})==1,"original localization index lookup");
            }
        }
        {
            const char *builder="_ZN13GameMpManager20MP_ServerSendEndGameEv";
            nova::abi::GuestCall send(image,builder,0x36,"_ZN13GameMpManager22MP_SendCommandsMessageEv",ptr(reinterpret_cast<void *>(&capture_end)));
            std::array<uint32_t,0xb144/4> mp{};
            const auto command=nova::abi::invoke(function("_Znwj"),{0x1c});
            nova::abi::invoke(udpCtor,{command,0x800});mp[0x4404/4]=command;
            nova::abi::invoke(function(builder),{ptr(mp.data())});
            check(nativeEnd,"original reliable end-game command");
            const auto replacement=mp[0x4404/4];
            nova::abi::invoke(word(word(replacement,0),4),{replacement});
        }
        // A minimal original Comms header fixture tests sparse ACKs, inclusive
        // bit zero, little-endian words and the unchanged native reader.
        std::array<uint32_t,0x3000/4> comms{};
        std::array<uint32_t,9> history{7,0,0,9,0,0,12,0,0};
        comms[0x2c1c/4]=0x12345678;comms[0x2c9c/4]=12;
        comms[0x2ea0/4]=ptr(history.data());comms[0x2ea4/4]=ptr(history.data()+history.size());
        comms[0x2ea8/4]=comms[0x2ea4/4];
        std::array<uint8_t,13> header{};
        nova::abi::invoke(function("_ZN5Comms12AddUDPHeaderEPhbi"),{ptr(comms.data()),ptr(header.data()),1,0});
        const auto expectedHeader=unhex("01785634120c00000029000000");
        check(std::memcmp(header.data(),expectedHeader.data(),13)==0,"original sparse inclusive ACK mask");
        std::array<uint32_t,4> fields{};
        nova::abi::invoke(function("_ZN5Comms13ReadUDPHeaderEPhiRiRjS2_S2_"),
            {ptr(comms.data()),ptr(header.data()),0,ptr(&fields[0]),ptr(&fields[1]),ptr(&fields[2]),ptr(&fields[3])});
        check(fields==std::array<uint32_t,4>{1,0x12345678,12,0x29},"original UDP header reader");
        // Original ACK scheduling needs at least two received reliable packets.
        // Waiting for an ACK of a joiner's only roster deadlocks loading.
        {
            std::array<uint32_t,0x3000/4> ackComms{};
            std::array<uint32_t,6> received{0,0,0,1,0,0};
            ackComms[0xdc/4]=2;
            ackComms[0x2ea0/4]=ptr(received.data());
            ackComms[0x2ea4/4]=ptr(received.data()+3);
            ackComms[0x2ea8/4]=ptr(received.data()+6);
            snapshotClock=5000;
            nova::abi::GuestHook clock(image,"_Z12OS_GetOSTimev",{0x55,0x89,0xe5,0x56,0x53},ptr(reinterpret_cast<void *>(&snapshot_clock)));
            const char *update="_ZN5Comms24UpdateReliableManagementEv";
            nova::abi::GuestCall drained(image,update,0x59,"_ZN5Comms20SendMessageFromQueueERSt6vectorI10PacketDataSaIS1_EEi",ptr(reinterpret_cast<void *>(&drained_send_queue)));
            nova::abi::GuestCall queue(image,update,0x1a7,"_ZN5Comms17PutMessageInQueueEPhjhbbb",ptr(reinterpret_cast<void *>(&capture_ack)));
            nova::abi::invoke(function(update),{ptr(ackComms.data())});
            check(nativeAckCount==0,"original single reliable packet has no idle ACK");
            ackComms[0x2ea4/4]=ptr(received.data()+6);
            snapshotClock=10000;
            nova::abi::invoke(function(update),{ptr(ackComms.data())});
            check(nativeAckCount==1,"original second reliable packet enables idle ACK");
            snapshotClock=217;
        }
        // Execute the real client type8 handler with synthetic manager/Comms
        // receivers. Only logging is supplied as an inert threshold object.
        // The decoder and RTT/clock writes remain original instructions.
        {
            std::array<uint32_t,0x3000/4> pingComms{};
            pingComms[0xdc/4]=2;
            pingComms[0x25c/4]=0x01020304;
            pingComms[0x15c/4]=42;
            pingComms[0x314/4]=1000;
            nova::abi::GuestHook clock(image,"_Z12OS_GetOSTimev",{0x55,0x89,0xe5,0x56,0x53},ptr(reinterpret_cast<void *>(&snapshot_clock)));
            const char *sendPing="_ZN5Comms14SendPingPachetEh";
            const char *handlePing="_ZN5Comms10HandlePingEPhi";
            nova::abi::GuestCall send(image,sendPing,0xf3,"_ZN5Comms17PutMessageInQueueEPhjhbbb",ptr(reinterpret_cast<void *>(&capture_ping)));
            nova::abi::GuestCall answer(image,handlePing,0x210,"_ZN5Comms17PutMessageInQueueEPhjhbbb",ptr(reinterpret_cast<void *>(&capture_ping)));
            check(nova::abi::invoke(function(sendPing),{ptr(pingComms.data()),0})==1,"original host ping builder");
            check(nativePing==unhex("0d010203040000002a"),"original ping sequence and previous RTT");
            auto request=nativePing;
            nova::abi::invoke(function(handlePing),{ptr(pingComms.data()),ptr(request.data()),0});
            check(nativePing==unhex("0d0102030400000000"),"original client replies to host ping");
            check(pingComms[0x15c/4]==42,"original client receives measured RTT");
            // UpdatePing must not originate client-side requests.
            nativePing.clear();
            snapshotClock=5000;
            nova::abi::invoke(function("_ZN5Comms10UpdatePingEv"),{ptr(pingComms.data())});
            check(nativePing.empty(),"original client ping scheduler is inactive");
            snapshotClock=217;
        }
        auto correction=unhex("0800000002fffffc18");
        std::array<uint32_t,9> reader{};
        std::array<uint32_t,0x3c00/4> manager{};
        manager[1]=ptr(comms.data());
        const auto readerCtor=function("_ZN16GamePacketReaderC1EPhi");
        nova::abi::invoke(readerCtor,{ptr(reader.data()),ptr(correction.data()),static_cast<uint32_t>(correction.size())});
        check(nova::abi::invoke(function("_ZN17GenericDataPacket18getGameMessageTypeEv"),{ptr(reader.data())})==8,"original clock type");
        auto *logCell=reinterpret_cast<uint32_t *>(image.symbol("_ZN10NetworkLog9SingletonE"));
        check(logCell!=nullptr,"network log symbol");
        const auto previousLog=*logCell;
        uint32_t silentLog=0x7fffffff;*logCell=ptr(&silentLog);
        nova::abi::invoke(function("_ZN9MpManager18HandleBaseMessagesEiP16GamePacketReaderi"),{ptr(manager.data()),8,ptr(reader.data()),0});
        *logCell=previousLog;
        check(comms[0x314/4]==2 && manager[0x5c/4]==1000 && reader[2]==reader[3],"original clock correction consumption");
        nova::abi::invoke(function("_ZN17GenericDataPacketD2Ev"),{ptr(reader.data())});
        {
            std::array<uint32_t,16> vtable{};
            vtable[0x3c/4]=ptr(reinterpret_cast<void *>(&capture_player_quit));
            manager[0]=ptr(vtable.data());manager[0x3a04/4]=1;
            auto quit=unhex("0c00");
            nova::abi::invoke(readerCtor,{ptr(reader.data()),ptr(quit.data()),static_cast<uint32_t>(quit.size())});
            check(nova::abi::invoke(function("_ZN17GenericDataPacket18getGameMessageTypeEv"),{ptr(reader.data())})==12,"original departure type");
            nova::abi::invoke(function("_ZN9MpManager18HandleBaseMessagesEiP16GamePacketReaderi"),{ptr(manager.data()),12,ptr(reader.data()),0});
            check(removedSlot==0 && reader[2]==reader[3],"original departure removes creator slot only");
            nova::abi::invoke(function("_ZN17GenericDataPacketD2Ev"),{ptr(reader.data())});
            manager[0]=0;
        }
        {
            // Execute the complete native command receiver and admission path.
            // Only graphics/loadout application and XP lookup are isolated;
            // packet parsing, roster, active status and CGameObject::Enable
            // remain original. This is not a rendered gameplay acceptance test.
            const char *handle="_ZN13GameMpManager22HandleIncomingMessagesEv";
            nova::abi::GuestHook queue(image,"_ZN9MpManager19QueueGetGameMessageERtRh",{0x55,0x89,0xe5,0x83,0xec,0x28},ptr(reinterpret_cast<void *>(&scene_queue)));
            nova::abi::GuestHook xp(image,"_ZN13GameMpManager31UpdateMpPlayerXPFromOnlineLobbyEPKc",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&scene_noop)));
            nova::abi::GuestHook component(image,"_ZNK11CGameObject12GetComponentEi",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&scene_component)));
            nova::abi::GuestHook loadout(image,"_ZN18MPLoadoutComponent10SetLoadoutER13PlayerLoadout",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&scene_loadout)));
            nova::abi::GuestHook skin(image,"_ZN21RemotePlayerComponent7SetSkinERKN6glitch5video6SColorE",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&scene_noop)));
            nova::abi::GuestHook respawn(image,"_ZN21RemotePlayerComponent7RespawnEv",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&scene_respawn)));
            nova::abi::GuestHook weapon(image,"_ZN21RemotePlayerComponent16SetCurrentWeaponEi",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&scene_weapon)));
            nova::abi::GuestHook notice(image,"_ZN9MpManager19AddNewConnectionMsgEPcii",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&scene_noop)));
            nova::abi::GuestHook events(image,"_ZN11CGameObject7OnEventEiPKv",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&scene_noop)));
            nova::abi::GuestCall local(image,handle,0x5150a9-0x50c340,"_ZN6CLevel18GetPlayerComponentEv",ptr(reinterpret_cast<void *>(&scene_local_player)));
            nova::abi::GuestCall level(image,handle,0x5150a1-0x50c340,"_ZN6CLevel8GetLevelEv",ptr(reinterpret_cast<void *>(&initial_level)));
            std::array<uint32_t,0xb144/4> mp{};
            std::array<std::array<uint32_t,0x200/4>,2> objects{},players{};
            std::array<uint32_t,0x50/4> sceneComms{};mp[1]=ptr(sceneComms.data());
            std::array<uint32_t,2> playerPointers{ptr(players[0].data()),ptr(players[1].data())};
            std::array<uint32_t,0x200/4> remote{},localPlayer{},localWeapon{};
            std::array<uint32_t,6> weaponRecord{23085,0,0,0,0,0};
            remote[1]=ptr(objects[1].data());sceneRemote=ptr(remote.data());sceneLocalPlayer=ptr(localPlayer.data());
            localPlayer[0x170/4]=ptr(localWeapon.data());localWeapon[0x148/4]=ptr(weaponRecord.data());
            mp[0x3bdc/4]=ptr(playerPointers.data());mp[0x3be0/4]=ptr(playerPointers.data()+2);
            for(size_t i=0;i<2;++i) players[i][0]=ptr(objects[i].data());
            auto *records=reinterpret_cast<uint8_t *>(mp.data());
            for(size_t i=0;i<12;++i) { records[0x37aa+i*0x34]=255;records[0x37ac+i*0x34]=255; }
            auto *singleton=reinterpret_cast<uint32_t *>(image.symbol("_ZN13GameMpManager9SingletonE"));
            const auto previousSingleton=*singleton;*singleton=ptr(mp.data());
            const auto previousSceneLog=*logCell;*logCell=ptr(&silentLog);
            udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});mp[0x4404/4]=ptr(udp.data());
            sceneMessage=unhex("1301010100000000000000000000000000000000523036506565720000000000000000000000000001020304");
            sceneConsumed=false;sceneRespawns=0;
            nova::abi::invoke(function(handle),{ptr(mp.data())});
            check(sceneRespawns==1 && remote[0x14/4]==0x01020304,"original newcomer admission and spawn");
            check(records[0x3795]==1 && records[0x37ac]==1,"original newcomer active roster");
            // Model the other client's fresh post-loading roster. Sending19
            // before56 here is a regression: both readers add the same actor.
            nova::abi::invoke(function("_ZN9MpManager19ResetPlayersDetailsEv"),{ptr(mp.data())});
            mp[0x3a04/4]=1;
            remote[1]=ptr(objects[0].data());sceneRespawns=0;sceneWeapon=0;
            sceneMessage.assign(781,0);sceneMessage[0]=56;sceneMessage[24]=67;
            for(size_t i=0;i<12;++i) {
                auto *row=sceneMessage.data()+25+i*63;row[2]=row[3]=255;
                if(i<2) {
                    row[0]=row[1]=1;row[2]=row[3]=static_cast<uint8_t>(i);
                    std::memcpy(row+20,i?"R06Peer":"R06Host",7);
                    row[57]=23085>>8;row[58]=23085&255;
                }
            }
            sceneConsumed=false;
            nova::abi::invoke(function(handle),{ptr(mp.data())});
            check(sceneRespawns==1 && sceneWeapon==23085,"original snapshot activates slot zero and selects valid weapon");
            check(records[0x3794]==1 && records[0x3794+0x34]==1 && records[0x3794+2*0x34]==0,
                "original snapshot creates exactly two occupied roster records");
            check(records[0x37ac]==0 && records[0x37ac+0x34]==1,"original snapshot retains distinct slots");
            check(players[0][0x124/4]==67 && players[1][0x124/4]==67,"original snapshot applies common render delay");
            check(*reinterpret_cast<uint8_t *>(udp[1]+13)==60,"original snapshot requests powerups");
            receivedLoadouts=0;
            sceneMessage=unhex("210001020304050607080901");sceneConsumed=false;
            nova::abi::invoke(function(handle),{ptr(mp.data())});
            check(receivedLoadouts==1 && receivedLoadout[0]==3 && receivedLoadout[1]==6 &&
                receivedLoadout[2]==4 && receivedLoadout[3]==5 && receivedLoadout[4]==7 && receivedLoadout[5]==8 &&
                receivedLoadout[6]==1 && receivedLoadout[7]==2 && receivedLoadout[8]==9 && (receivedLoadout[9]&255)==1,
                "original loadout receiver preserves field order");
            sceneConsumed=false;
            nova::abi::invoke(function(handle),{ptr(mp.data())});
            check(receivedLoadouts==1,"original unchanged loadout does not reapply equipment");
            // Execute both branches of31 and the original133 reset/request path.
            // Renderer/player respawn side effects are captured at their calls.
            localPlayer[1]=ptr(objects[1].data());localPlayer[0x108/4]=1;players[1][1]=1;
            std::array<uint32_t,0x58/4> playerVtable{};
            playerVtable[0x54/4]=ptr(reinterpret_cast<void *>(&scene_local_respawn));
            localPlayer[0]=ptr(playerVtable.data());sceneLocalRespawns=0;
            for(const char *spawn:{"1f000000750001020304","1f000000750105060708"}) {
                sceneMessage=unhex(spawn);sceneConsumed=false;
                nova::abi::invoke(function(handle),{ptr(mp.data())});
            }
            check(sceneRespawns==2 && remote[0x14/4]==0x01020304,"original remote round respawn");
            check(sceneLocalRespawns==1 && localPlayer[0x1a8/4]==0x05060708,"original local round respawn");
            {
                nova::abi::GuestHook freeze(image,"_ZN16CHealthComponent15MP_FreezePlayerEb",{0x55,0x89,0xe5,0x83,0xec,0x48},ptr(reinterpret_cast<void *>(&scene_unfreeze)));
                nova::abi::GuestHook loadoutRequest(image,"_ZN13GameMpManager26MP_ClientAddLoadoutMessageEP11CGameObject",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&scene_noop)));
                sceneMessage={133};sceneConsumed=false;sceneUnfreezes=0;
                const auto previousEnd=udp[2];
                nova::abi::invoke(function(handle),{ptr(mp.data())});
                check(records[0xafe8]==1 && records[0xafe9]==0 && sceneUnfreezes==2,"original round start resets and unfreezes both actors");
                check(udp[2]==previousEnd+2 && *reinterpret_cast<uint8_t *>(udp[1]+previousEnd)==32 &&
                    *reinterpret_cast<uint8_t *>(udp[1]+previousEnd+1)==1,"original round start requests local respawn");
            }
            {
                // Type135 is the clock tail of the original type60 response.
                // It completes admission even when no powerups are spawned.
                nova::abi::GuestHook clock(image,"_Z12OS_GetOSTimev",{0x55,0x89,0xe5,0x56,0x53},ptr(reinterpret_cast<void *>(&snapshot_clock)));
                for(const char *state:{"8701020304112233440000","8701020304112233440001","8701020304112233440100"}) {
                    sceneMessage=unhex(state);sceneConsumed=false;
                    nova::abi::invoke(function(handle),{ptr(mp.data())});
                    check(mp[0xafe0/4]==0x01020304 && mp[0xafdc/4]==0x11223344 && mp[0xafd8/4]==snapshotClock,
                        "original round clock epoch/elapsed/receipt time");
                    check(records[0xafe8]==sceneMessage[9] && records[0xafe9]==sceneMessage[10],"original running/countdown flags");
                }
            }
            {
                nova::abi::GuestHook kills(image,"_ZN13GameMpManager17UpdateWeaponKillsEiiibbi",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&scene_noop)));
                nova::abi::GuestHook death(image,"_ZN13GameMpManager25MP_ClientManageDeathStateEiiibiiRKN6glitch4core8vector3dIfEE",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&scene_death)));
                sceneMessage=unhex("1d010012340f0539");sceneConsumed=false;
                nova::abi::invoke(function(handle),{ptr(mp.data())});
                check(deathFields==std::array<uint32_t,6>{1,0,0x1234,1,5,3},"original death decoder fields");
            }
            nova::abi::invoke(function("_ZN17GenericDataPacketD2Ev"),{ptr(udp.data())});
            *singleton=previousSingleton;*logCell=previousSceneLog;
            sceneMessage.clear();sceneRemote=sceneLocalPlayer=0;
        }
        auto start=unhex("12000007d0112233441234567801020304");
        nova::abi::invoke(readerCtor,{ptr(reader.data()),ptr(start.data()),static_cast<uint32_t>(start.size())});
        check(nova::abi::invoke(function("_ZN17GenericDataPacket18getGameMessageTypeEv"),{ptr(reader.data())})==18,"original start type");
        for(auto value:{2000U,0x11223344U,0x12345678U,0x01020304U})
            check(nova::abi::invoke(function("_ZN17GenericDataPacket6getIntEv"),{ptr(reader.data())})==value,"original start scalar order");
        check(reader[2]==reader[3],"original start consumed exactly");
        nova::abi::invoke(function("_ZN17GenericDataPacketD2Ev"),{ptr(reader.data())});
        {
            nova::abi::GuestHook clock(image,"_Z12OS_GetOSTimev",{0x55,0x89,0xe5,0x56,0x53},ptr(reinterpret_cast<void *>(&snapshot_clock)));
            nova::abi::GuestHook local(image,"_ZN9MpManager7IsLocalEv",{0x55,0x89,0xe5,0x53,0x83,0xec,0x14},ptr(reinterpret_cast<void *>(&snapshot_local)));
            nova::abi::GuestHook rotation(image,"_ZN11CGameObject11SetRotationERKN6glitch4core8vector3dIfEE",{0x55,0xb9,0x10,0,0,0},ptr(reinterpret_cast<void *>(&snapshot_rotation)));
            nova::net::NativeSnapshotPresentation presentation(image,&capture_snapshot);
            using Presentation=nova::net::NativeSnapshotPresentation;
            std::array<uint32_t,0xb144/4> mp{};
            std::array<uint32_t,0x190/4> info{};
            std::array<uint32_t,0x160/4> object{};
            std::array<uint32_t,0x1c0/4> remote{};
            std::array<uint32_t,0x190/4> otherInfo{};
            std::array<uint32_t,12> players{ptr(info.data()),ptr(otherInfo.data())};
            std::array<uint32_t,1> components{ptr(remote.data())};
            remote[0]=image.symbol("_ZTV21RemotePlayerComponent")+8;remote[1]=ptr(object.data());
            object[0xf4/4]=ptr(components.data());object[0xf8/4]=ptr(components.data()+1);
            info[0]=ptr(object.data());mp[0x3bdc/4]=ptr(players.data());
            const std::array<float,3> previousAngles{1.25f,-2.5f,19};
            std::memcpy(object.data()+0x60/4,previousAngles.data(),12);
            std::array<float,3> current{42,43,44},result{};
            uint8_t changed=0;
            const auto call=function("_ZN21RemotePlayerComponent26UpdateRemotePlayerPositionEi")+0xcd;
            check(*reinterpret_cast<uint8_t *>(call)==0xe8,"snapshot call boundary");
            const auto replacement=call+5+word(call,1);
            auto render=[&] {
                changed=99;
                check(nova::abi::invoke(replacement,{ptr(result.data()),ptr(mp.data()),0,ptr(&changed),ptr(current.data()),ptr(info.data()+0x108/4),ptr(info.data()+0x114/4)})==ptr(result.data()),"snapshot sret pointer");
            };
            std::array<nova::net::pc::Snapshot,2> samples{};
            samples[0]={0,1,1,100,{0,0,0},*nova::net::pc::encode_angle(350),*nova::net::pc::encode_angle(330)};
            samples[1]={0,1,2,200,{10,20,30},*nova::net::pc::encode_angle(10),*nova::net::pc::encode_angle(30)};
            Presentation::begin(false);Presentation::receive(samples);render();
            check(result==current && !changed,"snapshot cannot implicitly spawn an actor");
            check(Presentation::spawn(0,1),"snapshot initial generation");render();
            check(result==current && !changed,"empty admitted history holds current position");
            Presentation::receive(samples);render();
            check(result==std::array<float,3>{5,10,15} && changed,"shared 67 ms position timeline");
            std::array<float,3> angles{};std::memcpy(angles.data(),object.data()+0x60/4,12);
            float pitch=0;std::memcpy(&pitch,remote.data()+0x84/4,4);
            check(angles[0]==1.25f && angles[1]==-2.5f && std::abs(angles[2])<.001f && std::abs(pitch)<.001f,"shared yaw/pitch timeline and wrap");
            check(snapshotAligned,"snapshot host-to-game stack alignment");
            check(!Presentation::spawn(0,1) && Presentation::spawn(0,2) && !Presentation::spawn(0,1),"snapshot generation ordering");
            Presentation::receive(samples);render();
            check(result==current && !changed,"old generation does not interpolate across respawn");
            samples[0].generation=2;samples[0].position={100,101,102};
            Presentation::receive(std::span(samples).first(1));render();
            check(result==samples[0].position,"new life starts with its own history");
            // Execute the actual sender entry with native history layout. Its
            // fractional angles must survive before native byte quantization.
            const auto sender=function("_ZN13GameMpManager26ClientSendPositionsMessageEv");
            const std::array<float,4> ownPosition{1.25f,-2.5f,3.75f,0};
            info[0x114/4]=ptr(ownPosition.data());info[0x118/4]=ptr(ownPosition.data()+4);
            const std::array<float,3> ownAngles{-0.125f,0,359.875f};
            std::memcpy(object.data()+0x60/4,ownAngles.data(),12);
            mp[0x5c/4]=100;
            nova::abi::invoke(sender,{ptr(mp.data())});
            check(capturedSnapshots.size()==1,"original sender routes one precise proposal");
            const auto &proposal=capturedSnapshots.back();
            check(proposal.slot==0 && proposal.generation==2 && proposal.sequence==1 && proposal.time==117 &&
                  proposal.position==std::array<float,3>{1.25f,-2.5f,3.75f} &&
                  proposal.yaw==*nova::net::pc::encode_angle(359.875) && proposal.pitch==*nova::net::pc::encode_angle(-0.125),
                  "native capture preserves coordinates, clock and fractional angles");
            check(Presentation::spawn(0,3),"sender new life");
            nova::abi::invoke(sender,{ptr(mp.data())});
            check(capturedSnapshots.size()==2 && capturedSnapshots.back().sequence==1 && capturedSnapshots.back().generation==3,
                  "sender respawn resets cadence and sequence");
            // Execute the original connectivity calculation with two native
            // roster entries. Snapshot traffic must replace the accounting
            // lost with the coarse position handler, not disable the watchdog.
            auto *roster=reinterpret_cast<uint8_t *>(mp.data());
            for(unsigned i=0;i<12;++i) roster[0x37ac+i*0x34]=0xff;
            for(unsigned i=0;i<2;++i) { roster[0x3795+i*0x34]=1;roster[0x37ac+i*0x34]=uint8_t(i); }
            check(Presentation::spawn(1,1),"remote connectivity life");
            auto traffic=samples;
            for(auto &v:traffic) { v.slot=1;v.generation=1; }
            Presentation::receive(traffic);
            nova::abi::invoke(sender,{ptr(mp.data())});
            check(otherInfo[0x140/4]==2,"fresh snapshots count once on the game thread");
            mp[0xaff4/4]=1;mp[0xafec/4]=5;snapshotClock=2217;
            const auto connectivity=function("_ZN13GameMpManager25ClientComputeConnectivityEv");
            nova::abi::invoke(connectivity,{ptr(mp.data())});
            check(!roster[0xafeb] && mp[0xafec/4]==0xffffffff && otherInfo[0x140/4]==0,
                  "original watchdog accepts precise traffic and resets its interval");
            Presentation::receive(traffic); // Duplicate snapshots must not mask loss.
            nova::abi::invoke(sender,{ptr(mp.data())});render();
            check(otherInfo[0x140/4]==0,"duplicates and held rendering are not connectivity");
            mp[0xafec/4]=5;snapshotClock+=1100;
            nova::abi::invoke(connectivity,{ptr(mp.data())});
            check(roster[0xafeb]==1,"original watchdog still detects a genuine outage");
            for(auto &v:traffic) { v.sequence+=2;v.time+=200; }
            Presentation::receive(traffic);
            nova::abi::invoke(sender,{ptr(mp.data())});snapshotClock+=1100;
            nova::abi::invoke(connectivity,{ptr(mp.data())});
            check(!roster[0xafeb] && mp[0xafec/4]==0xffffffff,"fresh traffic recovers native connectivity");
            for(auto &v:traffic) { v.sequence+=2;v.time+=200; }
            Presentation::receive(traffic);check(Presentation::spawn(1,2),"remote new life clears pending traffic");
            nova::abi::invoke(sender,{ptr(mp.data())});
            check(otherInfo[0x140/4]==0,"old-life traffic does not credit a new life");
            snapshotClock=217;
            info[0x114/4]=info[0x118/4]=0;mp[0x5c/4]=0;
            snapshotLocal=1;render();check(result==current,"online state cannot affect Wi-Fi presentation");
            Presentation::begin(true);check(Presentation::spawn(0,2),"LAN presentation generation");
            Presentation::receive(std::span(samples).first(1));render();
            check(result==samples[0].position,"LAN reuses the same presentation path");
            Presentation::clear();render();check(result==current,"revoked presentation returns to native path");
            snapshotLocal=0;
            // Original full respawn builders, with real component lookup and
            // power-up reset, share goldens with the independent Go codec.
            auto *singleton=reinterpret_cast<uint32_t *>(image.symbol("_ZN13GameMpManager9SingletonE"));
            check(singleton!=nullptr,"native MP singleton");
            struct Restore { uint32_t *cell,value; ~Restore(){*cell=value;} } restore{singleton,*singleton};
            *singleton=ptr(mp.data());
            mp[0x5c/4]=100;mp[0x4404/4]=ptr(udp.data());
            info[0xfc/4]=7;info[0x180/4]=9;
            udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
            nova::abi::invoke(function("_ZN13GameMpManager26MP_ServerAddRespawnMessageEP11CGameObjecti"),{ptr(mp.data()),ptr(object.data()),0x01020304});
            finish("1f000000750001020304");
            check(info[0xfc/4]==0 && info[0x180/4]==0 && info[0x17c/4]==500,"native respawn side effects");
            udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
            nova::abi::invoke(function("_ZN13GameMpManager33MP_ClientAddRequestRespawnMessageEP11CGameObject"),{ptr(mp.data()),ptr(object.data())});
            finish("2000");
        }
        {
            std::array<uint32_t,0xb144/4> mp{};mp[0x4404/4]=ptr(udp.data());
            udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
            nova::abi::invoke(function("_ZN13GameMpManager27MP_ClientSendsPurchasedInfoEih"),{ptr(mp.data()),1,0xa5});
            finish("a301a5");
            for(uint32_t flags=0;flags<16;++flags) {
                std::array<float,3> direction{1,0,0};
                udp.fill(0);nova::abi::invoke(udpCtor,{ptr(udp.data()),0xffffff});
                nova::abi::invoke(function("_ZN13GameMpManager22MP_ServerAddDeadPlayerEiiibibbbiN6glitch4core8vector3dIfEE"),
                    {ptr(mp.data()),1,0,0x1234,flags&1,5,(flags>>1)&1,(flags>>2)&1,(flags>>3)&1,3,ptr(direction.data())});
                char expected[17]{};std::snprintf(expected,sizeof(expected),"1d01001234%02x0530",flags);
                finish(expected);
            }
        }
        {
            nova::abi::GuestHook dead(image,"_ZNK11CGameObject6IsDeadEv",{0x55,0x89,0xe5,0x53,0x83,0xec,0x14},ptr(reinterpret_cast<void *>(&scene_noop)));
            nova::abi::GuestHook component(image,"_ZNK11CGameObject12GetComponentEi",{0x55,0x89,0xe5,0x57,0x56},ptr(reinterpret_cast<void *>(&scene_noop)));
            nova::abi::GuestHook identity(image,"_ZN13GameMpManager21GetIdAndTypeForObjectEP11CGameObjectRiS2_",{0x55,0x89,0xe5,0x83,0xec,0x38},ptr(reinterpret_cast<void *>(&damage_identity)));
            nova::abi::GuestHook height(image,"_ZNK11CGameObject9GetHeightEv",{0x55,0x89,0xe5,0x53,0x83,0xec,0x14},ptr(reinterpret_cast<void *>(&damage_height)));
            nova::abi::GuestHook effect(image,"_ZN13GameMpManager29createHitEffectCodeFromVectorEN6glitch4core8vector3dIfEEf",{0x55,0x89,0xe5,0x56,0x53},ptr(reinterpret_cast<void *>(&damage_effect)));
            std::array<uint32_t,0xb144/4> mp{};
            std::array<std::array<uint32_t,0x200/4>,3> objects{},players{};
            std::array<uint32_t,3> playerPointers{};
            std::array<std::array<uint32_t,0x50/4>,3> packets{};
            auto *rows=reinterpret_cast<uint8_t *>(mp.data());
            for(size_t i=0;i<12;++i) rows[0x37ac+i*0x34]=255;
            for(size_t i=0;i<3;++i) {
                playerPointers[i]=ptr(players[i].data());players[i][0]=ptr(objects[i].data());players[i][1]=i;
                rows[0x37ac+i*0x34]=static_cast<uint8_t>(i);rows[0x3795+i*0x34]=1;
                mp[(0xa40c+i*4)/4]=ptr(packets[i].data());
            }
            mp[0x3bdc/4]=ptr(playerPointers.data());mp[0x3be0/4]=ptr(playerPointers.data()+3);
            damageAttacker=ptr(objects[0].data());
            std::array<uint32_t,0x80/4> damage{};damage[1]=2;damage[3]=1;damage[0x3c/4]=damageAttacker;damage[0x7c/4]=3;
            *reinterpret_cast<float *>(&damage[0x18/4])=1;
            std::array<float,3> hit{};
            for(const auto amount:{7.5f,8.0f,25.0f,300.0f}) {
                for(size_t i=1;i<3;++i) { packets[i].fill(0);nova::abi::invoke(udpCtor,{ptr(packets[i].data()),0xffffff}); }
                *reinterpret_cast<float *>(damage.data())=amount;damageTargetType=0;
                nova::abi::invoke(function("_ZN13GameMpManager25MP_ServerAddDamageMessageEP11CGameObject7CDamageRKN6glitch4core8vector3dIfEE"),{ptr(mp.data()),ptr(objects[1].data()),ptr(damage.data()),ptr(hit.data())});
                const uint8_t encoded=amount==7.5f?240:amount==8?0:amount==25?25:255;
                const std::array<uint8_t,7> expected{21,0,1,static_cast<uint8_t>(amount<=8?7:6),encoded,0x20,0x21};
                check(packets[1][2]==20 && std::memcmp(reinterpret_cast<void *>(packets[1][1]+13),expected.data(),expected.size())==0,"original victim damage flags and quantization");
                const std::array<uint8_t,4> observer{22,0,1,3};
                check(packets[2][2]==17 && std::memcmp(reinterpret_cast<void *>(packets[2][1]+13),observer.data(),observer.size())==0,"original observer hit uses target part");
                for(size_t i=1;i<3;++i) nova::abi::invoke(function("_ZN17GenericDataPacketD2Ev"),{ptr(packets[i].data())});
            }
            for(size_t i=1;i<3;++i) { packets[i].fill(0);nova::abi::invoke(udpCtor,{ptr(packets[i].data()),0xffffff}); }
            *reinterpret_cast<float *>(damage.data())=25;damageTargetType=1;
            nova::abi::invoke(function("_ZN13GameMpManager25MP_ServerAddDamageMessageEP11CGameObject7CDamageRKN6glitch4core8vector3dIfEE"),{ptr(mp.data()),ptr(objects[1].data()),ptr(damage.data()),ptr(hit.data())});
            for(size_t i=1;i<3;++i) {
                const std::array<uint8_t,5> expected{23,0,1,25,3};
                check(packets[i][2]==18 && std::memcmp(reinterpret_cast<void *>(packets[i][1]+13),expected.data(),expected.size())==0,"original object hit includes amount before target part");
                nova::abi::invoke(function("_ZN17GenericDataPacketD2Ev"),{ptr(packets[i].data())});
            }
        }
        {
            // Isolate the animation-state lookup and select the early-return
            // branch after the original cooldown write. No shots are spawned.
            nova::abi::GuestHook state(image,"_ZN19CStatesSetComponent25GetCurrentStateTypeOnSlotEi",{0x55,0x89,0xe5,0x83,0xec,0x28},ptr(reinterpret_cast<void *>(&scene_noop)));
            std::array<uint32_t,0x2a8/4> component{};
            std::array<uint32_t,0x130/4> config{};
            std::array<uint32_t,0x200/4> object{};
            std::array<uint32_t,0x58/4> mode{};
            uint32_t ammo=10,cooldown=0,secondary=0;
            component[1]=ptr(object.data());component[0x14/4]=ptr(config.data());
            component[0x44/4]=ptr(&ammo);component[0x214/4]=ptr(&cooldown);component[0x22c/4]=ptr(&secondary);
            *reinterpret_cast<float *>(&component[0x244/4])=1.25f;
            *reinterpret_cast<float *>(&component[0x250/4])=1.5f;
            config[0x1c/4]=ptr(mode.data());config[0x20/4]=ptr(mode.data()+mode.size());config[0x40/4]=1;
            mode[0x10/4]=120;mode[0xc/4]=350;mode[8/4]=1;
            check(nova::abi::invoke(function("_ZN16CWeaponComponent16HasContinousFireEi"),{ptr(component.data()),0})==1 &&
                nova::abi::invoke(function("_ZN16CWeaponComponent21GetContinousFireDelayEi"),{ptr(component.data()),0})==350,"original continuous mode flag/delay fields");
            for(uint32_t kind:{23085U,23092U}) {
                object[0x88/4]=kind;cooldown=0;
                nova::abi::invoke(function("_ZN16CWeaponComponent10FireWeaponEP11CGameObjectbi"),{ptr(component.data()),1,0,0});
                check(cooldown==(kind==23092?180:96),"original base fire delay and modifier branch");
                mode[0x10/4]=999;
                nova::abi::invoke(function("_ZN16CWeaponComponent10FireWeaponEP11CGameObjectbi"),{ptr(component.data()),1,0,0});
                check(cooldown==(kind==23092?180:96),"active cooldown blocks another fire transition");
                mode[0x10/4]=120;
            }
        }
        auto release=[&](uint32_t p){ if(p) nova::abi::invoke(function("_ZdlPv"),{p}); };
        for(auto p:playerSlots) if(p) {release(word(p,0));release(word(p,4));release(word(p,0xc));release(p);}
        release(word(room,4));release(word(room,8));release(word(room,0x20));release(room);
        nova::abi::invoke(function("_ZN15DataPacketLobbyD1Ev"),{ptr(packet.data())});
        std::puts("PASS original lobby/game codecs, clock correction handler and start scalar readers");
        return 0;
    } catch(const std::exception &e) {std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
}
