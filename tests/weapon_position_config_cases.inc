// Included inside the existing isolated fov-config fixture, after its IO seam.
// No real profile path. All operations below remain authored/unexecuted by V01s.
{
    using nova::weapon_placement::Position;
    using nova::weapon_placement::RequestedPosition;
    const Position requested{std::bit_cast<float>(0x3ccc0001u),-.04f};
    const std::string untouched="# position\r\nworld_fov=native\r\nweapon_fov=104\n"
        "render_resolution=3840x2160\r\nfullscreen=false\nwindow_size=1280x720\r\n"
        "language=en\n";
    const std::string source=untouched+"  weapon_position_x = 0.01  \r\nweapon_position_x=-0.02\nweapon_position_y=0";
    fault=Fault::none;
    write(config_path,untouched);
    auto restored=nova::profile::read_config(config_path,obb);
    check(restored.weapon_position==Position{} && !restored.world_fov && restored.weapon_fov==104.f &&
          read(config_path)==untouched,"missing position keys default0 without startup rewrite or FOV changes");
    write(config_path,"weapon_position_y=-0.04\n");
    check(nova::profile::read_config(config_path,obb).weapon_position==Position{0,-.04f},
          "independently missing X defaults0 while explicit Y survives");
    write(config_path,"weapon_position_x=0.02\n");
    check(nova::profile::read_config(config_path,obb).weapon_position==Position{.02f,0},
          "independently missing Y defaults0 while explicit X survives");
    const auto merged=merge_weapon_position_config(source,requested);
    check(merged.starts_with(untouched) && merged.find("  weapon_position_x = ")!=std::string::npos &&
          merged.find("  \r\nweapon_position_x=")!=std::string::npos && merged.back()!='\n',
          "position merge preserves unrelated bytes, duplicate keys, whitespace and unterminated line");
    check(merged.find("= 0.01")==std::string::npos && merged.find("=-0.02")==std::string::npos,
          "all duplicate owned coordinates replaced");
    write(config_path,merged); restored=nova::profile::read_config(config_path,obb);
    check(std::bit_cast<uint32_t>(restored.weapon_position.x)==std::bit_cast<uint32_t>(requested.x) &&
          restored.weapon_position.y==requested.y,"explicit pair round-trips actual parser at float precision");
    check(merge_weapon_position_config(merged,requested)==merged,"position merge idempotent");
    check(merge_fov_config(merged,{}).find("weapon_position_y=-")!=std::string::npos &&
          merge_display_config(merged,{}).find("weapon_position_y=-")!=std::string::npos &&
          merge_render_config(merged,{1280,720}).find("weapon_position_y=-")!=std::string::npos,
          "FOV/Display/render saves accept and retain explicit position keys");
    for(auto bad:{"nan","inf","-inf","0.05001","-0.05001","broken","0.02tail",""}) {
        const auto invalid="weapon_position_x=0.01\nweapon_position_y="+std::string(bad)+"\n";
        bool rejected=false;
        try { (void)merge_weapon_position_config(invalid,{}); } catch(const std::exception &) { rejected=true; }
        check(rejected,"malformed stored coordinate cannot silently become0 on merge");
        write(config_path,invalid); rejected=false;
        try { (void)nova::profile::read_config(config_path,obb); } catch(const std::exception &) { rejected=true; }
        check(rejected && read(config_path)==invalid,"startup rejects corrupt pair without writing it");
        const auto failed=save_weapon_position_config(config_path,requested,&io);
        check(!failed.saved && !failed.error.empty() && read(config_path)==invalid,
              "invalid current pair fails transaction before changing destination");
    }
    write(config_path,source);
    check(!save_weapon_position_config(config_path,{.01f,std::numeric_limits<float>::infinity()},&io).saved &&
          read(config_path)==source,"invalid requested Y cannot partially persist valid X");
    write(config_path,"world_fov=native\nweapon_fov=native\n");
    check(save_weapon_position_config(config_path,{-0.f,-0.f},&io).saved &&
          read(config_path).find("weapon_position_x = 0\nweapon_position_y = 0\n")!=std::string::npos,
          "explicit negative-zero pair persists canonical original0");
    RequestedPosition host(requested);
    for(bool existing:{false,true}) for(auto injected:{Fault::none,Fault::create,Fault::write,Fault::flush,
            Fault::close,Fault::replace,Fault::external}) {
        fault=Fault::none; std::filesystem::remove(config_path);
        if(existing) write(config_path,source);
        temp_handle=INVALID_HANDLE_VALUE; temp_path.clear(); removed=0; fault=injected;
        const auto saved_position=save_weapon_position_config(config_path,host.requested(),&io);
        check(saved_position.saved==(injected==Fault::none),"position pair uses same atomic IO transaction");
        check(host.requested()==requested,"failed persistence never discards requested pair");
        if(injected!=Fault::none) {
            check(!saved_position.error.empty(),"position save failure is visible for explicit retry");
            if(injected==Fault::external) check(read(config_path).find("language = external")!=std::string::npos,
                                                "concurrent edit is retained");
            else check(existing ? read(config_path)==source : !std::filesystem::exists(config_path),
                       "failed X/Y save preserves complete prior pair or absence");
            check(!std::filesystem::exists(temp_path),"owned temporary cleaned after rejected pair save");
            fault=Fault::none;
            check(save_weapon_position_config(config_path,host.requested(),&io).saved,"explicit pair retry succeeds");
            if(injected==Fault::external) check(read(config_path).find("language = external")!=std::string::npos,
                                                "retry merges current external unrelated settings");
        }
        restored=nova::profile::read_config(config_path,obb);
        check(restored.weapon_position==requested,"success/retry persists coherent X and Y");
    }
    fault=Fault::none; write(config_path,merged); host.reset();
    check(save_weapon_position_config(config_path,host.requested(),&io).saved,"Reset pair saved atomically");
    restored=nova::profile::read_config(config_path,obb);
    check(restored.weapon_position==Position{} && !restored.world_fov && restored.weapon_fov==104.f &&
          read(config_path).starts_with(untouched),"position Reset0 preserves Native FOV/max104 and Display bytes");
#ifdef NOVA_V01S_O08G_COEXIST
    const auto with_movement=merged+"\nmovement_wobble = 37\r\n";
    const auto moved=merge_weapon_position_config(with_movement,{-.01f,.02f});
    check(moved.find("movement_wobble = 37\r\n")!=std::string::npos,"V01 save preserves O08 requested strength bytes");
    const auto wobbled=merge_movement_wobble_config(moved,nova::movement_wobble::Strength::from_percent(50));
    write(config_path,wobbled); restored=nova::profile::read_config(config_path,obb);
    check(restored.weapon_position==Position{-.01f,.02f} && restored.movement_wobble.percent()==50,
          "O08 save preserves both V01 coordinates through actual combined parser");
#endif
    fault=Fault::none; std::filesystem::remove(config_path);
}
