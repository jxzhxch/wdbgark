/*
    * WinDBG Anti-RootKit extension
    * Copyright � 2013-2015  Vyacheslav Rusakoff
    * 
    * This program is free software: you can redistribute it and/or modify
    * it under the terms of the GNU General Public License as published by
    * the Free Software Foundation, either version 3 of the License, or
    * (at your option) any later version.
    * 
    * This program is distributed in the hope that it will be useful,
    * but WITHOUT ANY WARRANTY; without even the implied warranty of
    * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    * GNU General Public License for more details.
    * 
    * You should have received a copy of the GNU General Public License
    * along with this program.  If not, see <http://www.gnu.org/licenses/>.

    * This work is licensed under the terms of the GNU GPL, version 3.  See
    * the COPYING file in the top-level directory.
*/

#if _MSC_VER > 1000
#pragma once
#endif

#ifndef WDBGARK_HPP_
#define WDBGARK_HPP_

#undef EXT_CLASS
#define EXT_CLASS wa::WDbgArk
#include <engextcpp.hpp>

#if defined(_DEBUG)
    #define _CRTDBG_MAP_ALLOC
    #include <stdlib.h>
    #include <crtdbg.h>
#endif  // _DEBUG

#include <string>
#include <sstream>
#include <map>
#include <vector>
#include <memory>
#include <unordered_map>
#include <set>
#include <utility>

#include "objhelper.hpp"
#include "colorhack.hpp"
#include "dummypdb.hpp"
#include "ver.hpp"

namespace wa {
//////////////////////////////////////////////////////////////////////////
// main class
//////////////////////////////////////////////////////////////////////////
class WDbgArk : public ExtExtension {
 public:
    //////////////////////////////////////////////////////////////////////////
    // class typedefs
    //////////////////////////////////////////////////////////////////////////
    struct SystemCbCommand {
        SystemCbCommand() : list_count_name(),
                            list_head_name(),
                            offset_to_routine(0),
                            list_count_address(0ULL),
                            list_head_address(0ULL) {}
        SystemCbCommand(std::string lcn,
                        std::string lhn,
                        unsigned __int32 oftr,
                        unsigned __int64 lca,
                        unsigned __int64 lha) : list_count_name(lcn),
                                                list_head_name(lhn),
                                                offset_to_routine(oftr),
                                                list_count_address(lca),
                                                list_head_address(lha) {}
        std::string      list_count_name;
        std::string      list_head_name;
        unsigned __int32 offset_to_routine;
        unsigned __int64 list_count_address;
        unsigned __int64 list_head_address;
    };

    typedef std::pair<std::string, SystemCbCommand> callbackPair;
    typedef std::map<std::string, SystemCbCommand> callbacksInfo;
    //////////////////////////////////////////////////////////////////////////
    typedef struct OutputWalkInfoTag {
        unsigned __int64 address;
        unsigned __int64 object_address;
        unsigned __int64 list_head_address;
        std::string      list_head_name;
        std::string      type;
        std::string      info;
    } OutputWalkInfo;

    typedef std::vector<OutputWalkInfo> walkresType;
    //////////////////////////////////////////////////////////////////////////
    typedef struct WalkCallbackContextTag {
        std::string      type;
        std::string      list_head_name;
        walkresType*     output_list_pointer;
        unsigned __int64 list_head_address;
    } WalkCallbackContext;
    //////////////////////////////////////////////////////////////////////////
    struct HalDispatchTablesInfo {
        HalDispatchTablesInfo() : hdt_count(0), hpdt_count(0), hiommu_count(0), skip(0) {}
        HalDispatchTablesInfo(unsigned __int8 hdt_c,
                              unsigned __int8 hpdt_c,
                              unsigned __int8 hio_c,
                              unsigned __int8 skip) : hdt_count(hdt_c),
                                                      hpdt_count(hpdt_c),
                                                      hiommu_count(hio_c),
                                                      skip(skip) {}
        unsigned __int8 hdt_count;      // HalDispatchTable table count
        unsigned __int8 hpdt_count;     // HalPrivateDispatchTable table count
        unsigned __int8 hiommu_count;   // HalIommuDispatch table count (W8.1+)
        unsigned __int8 skip;           // Skip first N entries
    };

    typedef std::map<unsigned __int32, HalDispatchTablesInfo> haltblInfo;
    //////////////////////////////////////////////////////////////////////////
    typedef HRESULT (*pfn_object_directory_walk_callback_routine)(WDbgArk* wdbg_ark_class,
                                                                  const ExtRemoteTyped &object,
                                                                  void* context);

    typedef HRESULT (*pfn_any_list_w_pobject_walk_callback_routine)(WDbgArk* wdbg_ark_class,
                                                                    const ExtRemoteData &object_pointer,
                                                                    void* context);

    typedef HRESULT (*pfn_device_node_walk_callback_routine)(WDbgArk* wdbg_ark_class,
                                                             const ExtRemoteTyped &device_node,
                                                             void* context);
    //////////////////////////////////////////////////////////////////////////
    WDbgArk();
    ~WDbgArk();

    HRESULT __thiscall Initialize(void) {
        m_ExtMajorVersion = VER_MAJOR;
        m_ExtMinorVersion = VER_MINOR;

        return S_OK;
    }

    // this one is called _before_ main class destructor, but ExtExtension class is already dead
    // so, don't output any errors in these routines, don't call g_Ext->m_Something and so on
    void __thiscall Uninitialize(void) {
        m_dummy_pdb->RemoveDummyPdbModule(m_symbols3_iface);    // unload dummypdb fake module
        RemoveSyntheticSymbols();                               // remove our symbols
        EXT_RELEASE(m_symbols3_iface);
    }

    //////////////////////////////////////////////////////////////////////////
    // main commands
    //////////////////////////////////////////////////////////////////////////
    EXT_COMMAND_METHOD(wa_ver);
    EXT_COMMAND_METHOD(wa_scan);
    EXT_COMMAND_METHOD(wa_systemcb);
    EXT_COMMAND_METHOD(wa_objtype);
    EXT_COMMAND_METHOD(wa_objtypeidx);
    EXT_COMMAND_METHOD(wa_objtypecb);
    EXT_COMMAND_METHOD(wa_callouts);
    EXT_COMMAND_METHOD(wa_pnptable);
    EXT_COMMAND_METHOD(wa_ssdt);
    EXT_COMMAND_METHOD(wa_w32psdt);
    EXT_COMMAND_METHOD(wa_checkmsr);
    EXT_COMMAND_METHOD(wa_idt);
    EXT_COMMAND_METHOD(wa_gdt);
    EXT_COMMAND_METHOD(wa_colorize);
    EXT_COMMAND_METHOD(wa_crashdmpcall);
    EXT_COMMAND_METHOD(wa_haltables);

    //////////////////////////////////////////////////////////////////////////
    // init
    //////////////////////////////////////////////////////////////////////////
    bool Init(void);
    bool IsInited(void) const { return m_inited; }
    bool IsLiveKernel(void) const {
        return ((m_DebuggeeClass == DEBUG_CLASS_KERNEL) && (m_DebuggeeQual == DEBUG_KERNEL_CONNECTION));
    }

    void RequireLiveKernelMode(void) const throw(...) {
        if ( !IsLiveKernel() ) { throw ExtStatusException(S_OK, "live kernel-mode only"); }
    }

    //////////////////////////////////////////////////////////////////////////
    // walk routines
    //////////////////////////////////////////////////////////////////////////
    void WalkExCallbackList(const std::string &list_count_name,
                            const unsigned __int64 offset_list_count,
                            const unsigned __int32 routine_count,
                            const std::string &list_head_name,
                            const unsigned __int64 offset_list_head,
                            const unsigned __int32 array_distance,
                            const std::string &type,
                            walkresType* output_list);

    void WalkAnyListWithOffsetToRoutine(const std::string &list_head_name,
                                        const unsigned __int64 offset_list_head,
                                        const unsigned __int32 link_offset,
                                        const bool is_double,
                                        const unsigned __int32 offset_to_routine,
                                        const std::string &type,
                                        const std::string &ext_info,
                                        walkresType* output_list);

    void WalkAnyListWithOffsetToObjectPointer(const std::string &list_head_name,
                                              const unsigned __int64 offset_list_head,
                                              const bool is_double,
                                              const unsigned __int32 offset_to_object_pointer,
                                              void* context,
                                              pfn_any_list_w_pobject_walk_callback_routine callback);

    void WalkDeviceNode(const unsigned __int64 device_node_address,
                        void* context,
                        pfn_device_node_walk_callback_routine callback);

    void WalkShutdownList(const std::string &list_head_name, const std::string &type, walkresType* output_list);
    void WalkPnpLists(const std::string &type, walkresType* output_list);
    void WalkCallbackDirectory(const std::string &type, walkresType* output_list);

    void WalkAnyTable(const unsigned __int64 table_start,
                      const unsigned __int32 offset_table_skip_start,
                      const unsigned __int32 table_count,
                      const std::string &type,
                      walkresType* output_list,
                      bool break_on_null = false,
                      bool collect_null = false);

    void AddSymbolPointer(const std::string &symbol_name,
                          const std::string &type,
                          const std::string &additional_info,
                          walkresType* output_list);

    void WalkDirectoryObject(const unsigned __int64 directory_address,
                             void* context,
                             pfn_object_directory_walk_callback_routine callback);

 private:
    //////////////////////////////////////////////////////////////////////////
    // callback routines
    //////////////////////////////////////////////////////////////////////////
    static HRESULT DirectoryObjectCallback(WDbgArk* wdbg_ark_class,
                                           const ExtRemoteTyped &object,
                                           void* context);

    static HRESULT ShutdownListCallback(WDbgArk* wdbg_ark_class,
                                        const ExtRemoteData &object_pointer,
                                        void* context);

    static HRESULT DirectoryObjectTypeCallback(WDbgArk* wdbg_ark_class,
                                               const ExtRemoteTyped &object,
                                               void* context);

    static HRESULT DirectoryObjectTypeCallbackListCallback(WDbgArk* wdbg_ark_class,
                                                           const ExtRemoteTyped &object,
                                                           void* context);

    static HRESULT DeviceNodeCallback(WDbgArk* wdbg_ark_class,
                                      const ExtRemoteTyped &device_node,
                                      void* context);

    //////////////////////////////////////////////////////////////////////////
    // helpers
    //////////////////////////////////////////////////////////////////////////
    void CallCorrespondingWalkListRoutine(const callbacksInfo::const_iterator &citer,
                                          walkresType* output_list);
    unsigned __int32 GetCmCallbackItemFunctionOffset() const;
    unsigned __int32 GetPowerCallbackItemFunctionOffset() const;
    unsigned __int32 GetPnpCallbackItemFunctionOffset() const;
    unsigned __int32 GetEmpCallbackItemLinkOffset() const;
    unsigned __int32 GetDbgkLkmdCallbackCount() const { return 0x08; }
    unsigned __int32 GetDbgkLkmdCallbackArrayDistance() const { return 2 * m_PtrSize; }
    bool             FindDbgkLkmdCallbackArray();
    unsigned __int32 GetCrashdmpCallTableCount() const;
    unsigned __int32 GetWindowsStrictMinorBuild(void) const;

    //////////////////////////////////////////////////////////////////////////
    // private inits
    //////////////////////////////////////////////////////////////////////////
    void CheckWindowsBuild(void);
    //////////////////////////////////////////////////////////////////////////
    void InitCallbackCommands(void);
    void InitCalloutNames(void);
    void InitGDTSelectors(void);
    void InitHalTables(void);
    void InitKnownWindowsBuilds(void);
    //////////////////////////////////////////////////////////////////////////
    void RemoveSyntheticSymbols(void);

    //////////////////////////////////////////////////////////////////////////
    // variables
    //////////////////////////////////////////////////////////////////////////
    bool             m_inited;
    bool             m_is_cur_machine64;
    unsigned __int32 m_platform_id;
    unsigned __int32 m_major_build;
    unsigned __int32 m_minor_build;
    unsigned __int32 m_strict_minor_build;
    unsigned __int32 m_service_pack_number;

    static const std::string          m_ms_public_symbols_server;
    callbacksInfo                     m_system_cb_commands;
    std::vector<std::string>          m_callout_names;
    std::vector<unsigned __int32>     m_gdt_selectors;
    haltblInfo                        m_hal_tbl_info;
    std::set<unsigned __int32>        m_known_windows_builds;
    std::vector<DEBUG_MODULE_AND_ID>  m_synthetic_symbols;
    std::unique_ptr<WDbgArkObjHelper> m_obj_helper;
    std::unique_ptr<WDbgArkColorHack> m_color_hack;
    std::unique_ptr<WDbgArkDummyPdb>  m_dummy_pdb;
    ExtCheckedPointer<IDebugSymbols3> m_symbols3_iface;
    //////////////////////////////////////////////////////////////////////////
    // output streams
    //////////////////////////////////////////////////////////////////////////
    std::stringstream out;
    std::stringstream warn;
    std::stringstream err;
};

}   // namespace wa

#endif  // WDBGARK_HPP_
