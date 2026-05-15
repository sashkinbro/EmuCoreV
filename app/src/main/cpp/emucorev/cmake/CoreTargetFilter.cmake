function(emucorev_remove_target_link target_name dependency_name)
    get_target_property(current_links "${target_name}" LINK_LIBRARIES)
    if(NOT current_links)
        return()
    endif()

    list(REMOVE_ITEM current_links "${dependency_name}")
    set_target_properties("${target_name}" PROPERTIES LINK_LIBRARIES "${current_links}")
endfunction()

function(emucorev_filter_vita3k_target target_name vendor_dir core_dir)
    if(NOT TARGET "${target_name}")
        message(FATAL_ERROR "Missing Vita3K native target: ${target_name}")
    endif()

    # Layer 2 owns integration policy. The vendored core can still define stock
    # UI targets, but EmuCoreV's Android library should not link them unless
    # the bridge explicitly needs them.
    foreach(stock_ui_target IN ITEMS gui gui-qt updater)
        if(TARGET "${stock_ui_target}")
            emucorev_remove_target_link("${target_name}" "${stock_ui_target}")
        endif()
    endforeach()

    if(TARGET config)
        target_include_directories(config PRIVATE "${core_dir}/renderer/include")
    endif()

    if(TARGET ime)
        target_include_directories(ime PUBLIC
            "${core_dir}/mem/include"
            "${core_dir}/util/include"
            "${core_dir}/lang/include"
            "${core_dir}/compat/include"
            "${vendor_dir}/external/imgui"
        )
    endif()
endfunction()
