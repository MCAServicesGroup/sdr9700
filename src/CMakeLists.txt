find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network Multimedia Sql Svg)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)
find_package(PkgConfig REQUIRED)

add_compile_options(-Wall -Wextra)

pkg_check_modules(OPUS REQUIRED IMPORTED_TARGET opus)
pkg_check_modules(SPEEXDSP REQUIRED IMPORTED_TARGET speexdsp)
if(sdr9700_ENABLE_HIDAPI)
    if(APPLE)
        pkg_check_modules(HIDAPI QUIET IMPORTED_TARGET hidapi)
    else()
        pkg_check_modules(HIDAPI QUIET IMPORTED_TARGET hidapi-hidraw)
        if(NOT HIDAPI_FOUND)
            pkg_check_modules(HIDAPI QUIET IMPORTED_TARGET hidapi)
        endif()
    endif()
else()
    set(HIDAPI_FOUND FALSE)
endif()
find_path(EIGEN3_INCLUDE_DIR
    NAMES Eigen/Dense
    PATH_SUFFIXES eigen3
    REQUIRED
)

# sdr9700_core: IC-9700 protocol stack
# Headers listed explicitly so AUTOMOC processes Q_OBJECT classes whose
# headers are not co-located with their .cpp files.
set(sdr9700_MOC_HEADERS
    audio/AudioConverter.h
    audio/AudioHandlerBase.h
    audio/AudioHandlerQtInput.h
    audio/AudioHandlerQtOutput.h
    core/CachingQueue.h
    radio/Commander.h
    radio/UdpAudio.h
    radio/UdpCivData.h
    radio/UdpHandler.h
    radio/RadioCommander.h
)

set(sdr9700_CORE_SOURCES
    ${sdr9700_MOC_HEADERS}

    # Radio / CI-V protocol
    radio/UdpStatusMessages.h
    radio/CivSequenceGate.h
    radio/CivSequenceGate.cpp
    radio/UdpBase.cpp
    radio/UdpHandler.cpp
    radio/UdpCivData.cpp
    radio/UdpAudio.cpp
    radio/Commander.cpp
    radio/RadioCommander.cpp

    # Core utilities
    core/CachingQueue.cpp
    core/AutomationServer.h
    core/AutomationServer.cpp
    core/ApplicationLog.h
    core/ApplicationLog.cpp
    core/Ax25Decoder.h
    core/Ax25Decoder.cpp
    core/AudioDeviceSelection.h
    core/AppPaths.cpp
    core/MemoryDatabase.h
    core/MemoryDatabase.cpp
    core/LogCategories.cpp
    core/LoggingConfiguration.h
    core/LoggingConfiguration.cpp
    core/RadioSessionWatchdog.h
    radio/RadioSessionOwnership.h
    backend/ReceiverReadinessPolicy.h
    radio/RadioSessionCorrelation.h
    radio/RadioSessionRecoveryStore.h
    radio/RetainedSessionRemovalPolicy.h
    radio/RadioSessionRecoveryStore.cpp
    core/SMeterScale.h
    core/Vfo.h
    core/AppSettings.cpp
    core/RadioProfile.cpp
    core/SystemStats.cpp
    core/Types.cpp

    # Audio - Qt Multimedia handlers only
    audio/AudioConverter.cpp
    audio/DtmfGenerator.cpp
    audio/TxAudioMonitoringPolicy.h
    audio/TxAudioPacing.h
    audio/AudioHandlerBase.cpp
    audio/AudioHandlerQtInput.cpp
    audio/AudioHandlerQtOutput.cpp
)

if(HIDAPI_FOUND)
    list(APPEND sdr9700_CORE_SOURCES
        core/IcomRC28Manager.h
        core/IcomRC28Manager.cpp
    )
endif()

configure_file(core/AppInfo.h.in core/AppInfo.h @ONLY)
configure_file(core/AppBuildConfig.h.in core/AppBuildConfig.h @ONLY)

add_library(sdr9700_core STATIC ${sdr9700_CORE_SOURCES})

# All role subdirs exposed as include roots so flat #include "Foo.h" resolves
# across the split layout without touching every file.
target_include_directories(sdr9700_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/radio
    ${CMAKE_CURRENT_SOURCE_DIR}/audio
    ${CMAKE_CURRENT_SOURCE_DIR}/core
    ${CMAKE_CURRENT_BINARY_DIR}/core
    ${EIGEN3_INCLUDE_DIR}
)

target_link_libraries(sdr9700_core PUBLIC
    Qt6::Core
    Qt6::Network
    Qt6::Multimedia
    Qt6::Sql
    OpenSSL::Crypto
    PkgConfig::OPUS
    PkgConfig::SPEEXDSP
)

if(HIDAPI_FOUND)
    target_link_libraries(sdr9700_core PUBLIC PkgConfig::HIDAPI)
    target_compile_definitions(sdr9700_core PUBLIC HAVE_HIDAPI)
endif()


# sdr9700 application implementation shared by the executable and GUI smoke tests.
set(sdr9700_APP_SOURCES
    backend/IRadioBackend.h
    backend/TransmitFrequencyPolicy.h
    backend/TransmitConfigurationPolicy.h
    backend/DualWatchTransitionPolicy.h
    backend/ConnectionRetryPolicy.h
    backend/StandbyWakePolicy.h
    backend/ScopeController.h
    backend/ScopeController.cpp
    backend/RadioRouter.h
    backend/RadioRouter.cpp
    backend/RadioBackend.h
    backend/RadioBackend.cpp

    models/RadioModel.h
    models/RadioModel.cpp
    models/RadioState.h
    models/RadioState.cpp
    models/VfoModel.h
    models/VfoModel.cpp
    models/SpectrumScopeModel.h
    models/SpectrumScopeModel.cpp
    models/MeterController.h
    models/MeterController.cpp

    gui/SpectrumScopeCanvas.h
    gui/SpectrumScopeCanvas.cpp
    gui/SpectrumScopeDisplay.h
    gui/SpectrumScopeDisplay.cpp
    gui/WaterfallCanvas.h
    gui/WaterfallCanvas.cpp
    gui/WaterfallController.h
    gui/WaterfallController.cpp
    gui/DialogPlacement.h
    gui/DialogFooter.h
    gui/ConfirmationDialog.h
    gui/UiTheme.h
    gui/MemoryStore.h
    gui/MemoryStore.cpp
    gui/MainWindowHelpers.h
    gui/MainWindowHelpers.cpp
    gui/SpectrumScopeController.h
    gui/SpectrumScopeController.cpp
    gui/MemoryCsvController.h
    gui/MemoryCsvController.cpp
    gui/MemoryCsvHelpers.h
    gui/MemoryEditorController.h
    gui/MemoryEditorController.cpp
    gui/MemoryEditorForm.h
    gui/MemoryEditorForm.cpp
    gui/MemoryConstants.h
    gui/MemoryRecordHelpers.h
    gui/MemoryEditorPolicy.h
    gui/MemoryViewHelpers.h
    gui/MemoryController.h
    gui/MemoryController.cpp
    gui/MemorySyncController.h
    gui/MemorySyncController.cpp
    gui/MemorySelectionController.h
    gui/MemorySelectionController.cpp
    gui/MemoryViewController.h
    gui/MemoryViewController.cpp
    gui/MemoryWriteController.h
    gui/MemoryWriteController.cpp
    gui/RadioCommandController.h
    gui/RadioCommandController.cpp
    gui/StatusBarController.h
    gui/StatusBarController.cpp
    gui/AutomationController.h
    gui/AutomationController.cpp
    gui/AutomationUiDriver.h
    gui/AutomationUiDriver.cpp
    gui/ConfigurationManager.h
    gui/ConfigurationManager.cpp
    gui/RadioChooserDialog.h
    gui/RadioChooserDialog.cpp
    gui/SettingsDialog.h
    gui/SettingsDialog.cpp
    gui/SettingsPanelStyle.h
    gui/ApplicationConfigurationSettingsPanel.h
    gui/ApplicationConfigurationSettingsPanel.cpp
    gui/MemoryManagerSettingsPanel.h
    gui/MemoryManagerSettingsPanel.cpp
    gui/SpectrumScopeSettingsPanel.h
    gui/SpectrumScopeSettingsPanel.cpp
    gui/AudioDevicesSettingsPanel.h
    gui/AudioDevicesSettingsPanel.cpp
    gui/AboutDialog.h
    gui/AboutDialog.cpp
    gui/ApplicationLogDialog.h
    gui/ApplicationLogDialog.cpp
    gui/DataDecoderDialog.h
    gui/DataDecoderDialog.cpp
    gui/DtmfDialog.h
    gui/DtmfDialog.cpp
    gui/MetersDialog.h
    gui/MetersDialog.cpp
    gui/MainTitleBar.h
    gui/MainTitleBar.cpp
    gui/VfoController.h
    gui/VfoController.cpp
    gui/VfoDisplay.h
    gui/VfoDisplay.cpp
    gui/VfoSMeter.h
    gui/VfoSMeter.cpp
    gui/VfoSelectionController.h
    gui/MainSubExchangePolicy.h
    gui/VfoSelectionController.cpp
    gui/VfoSelectionPanel.h
    gui/VfoSelectionPanel.cpp
    gui/MainWindow.h
    gui/MainWindow.cpp
)

if(APPLE)
    set(sdr9700_MACOS_ICON "${CMAKE_SOURCE_DIR}/resources/images/icons/sdr9700_app_icon.png")
    set_source_files_properties(${sdr9700_MACOS_ICON} PROPERTIES
        MACOSX_PACKAGE_LOCATION Resources
    )
endif()

if(HIDAPI_FOUND)
    list(APPEND sdr9700_APP_SOURCES
        gui/IcomRC28Controller.h
        gui/IcomRC28Controller.cpp
        gui/IcomRC28SettingsPanel.h
        gui/IcomRC28SettingsPanel.cpp
    )
endif()

add_library(sdr9700_app_support STATIC ${sdr9700_APP_SOURCES})
target_include_directories(sdr9700_app_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(sdr9700_app_support PUBLIC
    sdr9700_core
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
    Qt6::Multimedia
)
if(HIDAPI_FOUND)
    target_compile_definitions(sdr9700_app_support PUBLIC HAVE_HIDAPI)
endif()

set(sdr9700_EXECUTABLE_SOURCES
    main.cpp
    ../resources/qt/resources.qrc
)
if(APPLE)
    list(APPEND sdr9700_EXECUTABLE_SOURCES
        ${sdr9700_MACOS_ICON}
        platform/MacWindowRestoration.mm
    )
endif()

if(APPLE)
    add_executable(sdr9700 MACOSX_BUNDLE ${sdr9700_EXECUTABLE_SOURCES})
else()
    add_executable(sdr9700 ${sdr9700_EXECUTABLE_SOURCES})
endif()

target_include_directories(sdr9700 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sdr9700 PRIVATE
    sdr9700_app_support
    Qt6::Svg
)

if(APPLE AND Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
    # Darwin permission backends are non-default static Qt plugins. Import the
    # microphone backend explicitly so QMicrophonePermission can reach macOS
    # instead of falling back to Qt's "permission plugin not found" warning.
    include("${Qt6Core_DIR}/Qt6QDarwinMicrophonePermissionPluginConfig.cmake")
    qt_import_plugins(sdr9700 INCLUDE Qt6::QDarwinMicrophonePermissionPlugin)
endif()

set_target_properties(sdr9700 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

if(APPLE)
    set_target_properties(sdr9700 PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "sdr9700"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright sdr9700 contributors"
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.w5jwp.sdr9700"
        MACOSX_BUNDLE_ICON_FILE "sdr9700_app_icon.png"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/resources/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${sdr9700_DISPLAY_VERSION}"
    )
endif()

if(BUILD_TESTING)
    add_executable(AboutDialogTest tests/AboutDialogTest.cpp)
    target_include_directories(AboutDialogTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui)
    target_link_libraries(AboutDialogTest PRIVATE sdr9700_app_support Qt6::Test)
    add_test(NAME AboutDialogTest COMMAND AboutDialogTest)
    set_tests_properties(AboutDialogTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(Ax25DecoderTest tests/Ax25DecoderTest.cpp)
    target_link_libraries(Ax25DecoderTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME Ax25DecoderTest COMMAND Ax25DecoderTest)

    add_executable(ApplicationLogTest tests/ApplicationLogTest.cpp)
    target_link_libraries(ApplicationLogTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME ApplicationLogTest COMMAND ApplicationLogTest)

    add_executable(ApplicationLogDialogTest tests/ApplicationLogDialogTest.cpp)
    target_include_directories(ApplicationLogDialogTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui)
    target_link_libraries(ApplicationLogDialogTest PRIVATE sdr9700_app_support Qt6::Test)
    add_test(NAME ApplicationLogDialogTest COMMAND ApplicationLogDialogTest)
    set_tests_properties(ApplicationLogDialogTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(DataDecoderDialogTest tests/DataDecoderDialogTest.cpp)
    target_include_directories(DataDecoderDialogTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui)
    target_link_libraries(DataDecoderDialogTest PRIVATE sdr9700_app_support Qt6::Test)
    add_test(NAME DataDecoderDialogTest COMMAND DataDecoderDialogTest)
    set_tests_properties(DataDecoderDialogTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_library(sdr9700_memory_test_support STATIC
        gui/MemoryStore.cpp
        gui/MainWindowHelpers.cpp
    )
    target_include_directories(sdr9700_memory_test_support PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
    )
    target_link_libraries(sdr9700_memory_test_support PUBLIC
        sdr9700_core
        Qt6::Widgets
    )

    add_library(sdr9700_dtmf_dialog_test_support STATIC gui/DtmfDialog.cpp)
    target_include_directories(sdr9700_dtmf_dialog_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/gui)
    target_link_libraries(sdr9700_dtmf_dialog_test_support PUBLIC sdr9700_core Qt6::Widgets)

    if(HIDAPI_FOUND)
        add_executable(IcomRC28ManagerTest tests/IcomRC28ManagerTest.cpp)
        target_link_libraries(IcomRC28ManagerTest PRIVATE sdr9700_core Qt6::Test)
        add_test(NAME IcomRC28ManagerTest COMMAND IcomRC28ManagerTest)
    endif()

    add_executable(AudioConverterTest tests/AudioConverterTest.cpp)
    target_link_libraries(AudioConverterTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME AudioConverterTest COMMAND AudioConverterTest)

    add_executable(MemoryStoreTest
        tests/MemoryStoreTest.cpp
    )
    target_include_directories(MemoryStoreTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
    )
    target_link_libraries(MemoryStoreTest PRIVATE
        sdr9700_memory_test_support
        Qt6::Test
    )
    add_test(NAME MemoryStoreTest COMMAND MemoryStoreTest)

    add_executable(MemoryDatabaseTest tests/MemoryDatabaseTest.cpp)
    target_link_libraries(MemoryDatabaseTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME MemoryDatabaseTest COMMAND MemoryDatabaseTest)

    add_executable(MainWindowHelpersTest
        tests/MainWindowHelpersTest.cpp
    )
    target_include_directories(MainWindowHelpersTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
    )
    target_link_libraries(MainWindowHelpersTest PRIVATE
        sdr9700_memory_test_support
        Qt6::Test
    )
    add_test(NAME MainWindowHelpersTest COMMAND MainWindowHelpersTest)

    add_executable(VfoModelTest
        tests/VfoModelTest.cpp
        models/VfoModel.cpp
    )
    target_include_directories(VfoModelTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/models
    )
    target_link_libraries(VfoModelTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME VfoModelTest COMMAND VfoModelTest)

    add_executable(SpectrumScopeModelTest
        tests/SpectrumScopeModelTest.cpp
        models/SpectrumScopeModel.cpp
    )
    target_include_directories(SpectrumScopeModelTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/models
    )
    target_link_libraries(SpectrumScopeModelTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME SpectrumScopeModelTest COMMAND SpectrumScopeModelTest)

    add_executable(RadioCapabilitiesTest tests/RadioCapabilitiesTest.cpp)
    target_link_libraries(RadioCapabilitiesTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME RadioCapabilitiesTest COMMAND RadioCapabilitiesTest)

    add_executable(MeterControllerTest
        tests/MeterControllerTest.cpp
        models/MeterController.cpp
    )
    target_include_directories(MeterControllerTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/models
    )
    target_link_libraries(MeterControllerTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME MeterControllerTest COMMAND MeterControllerTest)

    add_executable(SettingsProfileTest tests/SettingsProfileTest.cpp)
    target_link_libraries(SettingsProfileTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME SettingsProfileTest COMMAND SettingsProfileTest)

    add_executable(SettingsProcessTest tests/SettingsProcessTest.cpp)
    target_link_libraries(SettingsProcessTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME SettingsProcessTest COMMAND SettingsProcessTest)

    add_executable(CachingQueueTest tests/CachingQueueTest.cpp)
    target_link_libraries(CachingQueueTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME CachingQueueTest COMMAND CachingQueueTest)

    add_executable(UdpBaseTest tests/UdpBaseTest.cpp)
    target_link_libraries(UdpBaseTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME UdpBaseTest COMMAND UdpBaseTest)

    add_executable(CivSequenceGateTest tests/CivSequenceGateTest.cpp)
    target_link_libraries(CivSequenceGateTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME CivSequenceGateTest COMMAND CivSequenceGateTest)

    add_executable(RadioSessionWatchdogTest tests/RadioSessionWatchdogTest.cpp)
    target_link_libraries(RadioSessionWatchdogTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME RadioSessionWatchdogTest COMMAND RadioSessionWatchdogTest)

    add_executable(WaterfallScopeTest
        tests/WaterfallScopeTest.cpp
        backend/ScopeController.cpp
        gui/WaterfallController.cpp
    )
    target_include_directories(WaterfallScopeTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/backend
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
    )
    target_link_libraries(WaterfallScopeTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME WaterfallScopeTest COMMAND WaterfallScopeTest)

    add_executable(RadioRouterTest
        tests/RadioRouterTest.cpp
        backend/RadioRouter.cpp
    )
    target_include_directories(RadioRouterTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/backend)
    target_link_libraries(RadioRouterTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME RadioRouterTest COMMAND RadioRouterTest)

    add_executable(CommanderCodecTest tests/CommanderCodecTest.cpp)
    target_link_libraries(CommanderCodecTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME CommanderCodecTest COMMAND CommanderCodecTest)

    add_executable(PacketLayoutTest tests/PacketLayoutTest.cpp)
    target_link_libraries(PacketLayoutTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME PacketLayoutTest COMMAND PacketLayoutTest)

    add_executable(OfflinePoliciesTest tests/OfflinePoliciesTest.cpp)
    target_include_directories(OfflinePoliciesTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/backend
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
    )
    target_link_libraries(OfflinePoliciesTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME OfflinePoliciesTest COMMAND OfflinePoliciesTest)

    add_executable(DtmfGeneratorTest tests/DtmfGeneratorTest.cpp)
    target_link_libraries(DtmfGeneratorTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME DtmfGeneratorTest COMMAND DtmfGeneratorTest)

    add_executable(DtmfDialogTest
        tests/DtmfDialogTest.cpp
    )
    target_include_directories(DtmfDialogTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui)
    target_link_libraries(DtmfDialogTest PRIVATE sdr9700_dtmf_dialog_test_support Qt6::Test)
    add_test(NAME DtmfDialogTest COMMAND DtmfDialogTest)
    set_tests_properties(DtmfDialogTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(SpectrumCanvasTest
        tests/SpectrumCanvasTest.cpp
        gui/SpectrumScopeCanvas.cpp
        gui/SpectrumScopeDisplay.cpp
        gui/WaterfallCanvas.cpp
        gui/WaterfallController.cpp
    )
    target_include_directories(SpectrumCanvasTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui)
    target_link_libraries(SpectrumCanvasTest PRIVATE sdr9700_core Qt6::Test Qt6::Widgets)
    add_test(NAME SpectrumCanvasTest COMMAND SpectrumCanvasTest)
    set_tests_properties(SpectrumCanvasTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(PanelAccessibilityTest
        tests/PanelAccessibilityTest.cpp
        gui/MetersDialog.cpp
    )
    target_include_directories(PanelAccessibilityTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/gui)
    target_link_libraries(PanelAccessibilityTest PRIVATE sdr9700_dtmf_dialog_test_support Qt6::Test)
    add_test(NAME PanelAccessibilityTest COMMAND PanelAccessibilityTest)
    set_tests_properties(PanelAccessibilityTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(VfoDisplayTest
        tests/VfoDisplayTest.cpp
        backend/IRadioBackend.h
        gui/MainWindowHelpers.cpp
        gui/VfoController.cpp
        gui/VfoDisplay.cpp
        gui/VfoSMeter.cpp
        gui/VfoSelectionPanel.cpp
        models/RadioState.cpp
        models/VfoModel.cpp
    )
    target_include_directories(VfoDisplayTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
    )
    target_link_libraries(VfoDisplayTest PRIVATE sdr9700_core Qt6::Test Qt6::Widgets)
    add_test(NAME VfoDisplayTest COMMAND VfoDisplayTest)
    set_tests_properties(VfoDisplayTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(VfoBackendTest
        tests/VfoBackendTest.cpp
        backend/IRadioBackend.h
        gui/MainWindowHelpers.cpp
        gui/VfoController.cpp
        gui/VfoDisplay.cpp
        gui/VfoSelectionController.cpp
        gui/VfoSelectionPanel.cpp
        gui/VfoSMeter.cpp
        models/RadioState.cpp
        models/VfoModel.cpp
    )
    target_include_directories(VfoBackendTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/backend
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
        ${CMAKE_CURRENT_SOURCE_DIR}/models
    )
    target_link_libraries(VfoBackendTest PRIVATE sdr9700_core Qt6::Test Qt6::Widgets)
    add_test(NAME VfoBackendTest COMMAND VfoBackendTest)
    set_tests_properties(VfoBackendTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(UdpStatusMessagesTest tests/UdpStatusMessagesTest.cpp)
    target_include_directories(UdpStatusMessagesTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(UdpStatusMessagesTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME UdpStatusMessagesTest COMMAND UdpStatusMessagesTest)

    set(GUI_SMOKE_TEST_SOURCES
        tests/GuiSmokeTest.cpp
        gui/SettingsDialog.cpp
        gui/ApplicationConfigurationSettingsPanel.cpp
        gui/AudioDevicesSettingsPanel.cpp
        gui/SpectrumScopeSettingsPanel.cpp
        gui/MemoryManagerSettingsPanel.cpp
        gui/ConfigurationManager.cpp
    )
    if(HIDAPI_FOUND)
        list(APPEND GUI_SMOKE_TEST_SOURCES gui/IcomRC28SettingsPanel.cpp)
    endif()
    add_executable(GuiSmokeTest ${GUI_SMOKE_TEST_SOURCES})
    target_include_directories(GuiSmokeTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
    )
    target_link_libraries(GuiSmokeTest PRIVATE
        sdr9700_memory_test_support
        Qt6::Test
    )
    add_test(NAME GuiSmokeTest COMMAND GuiSmokeTest)
    set_tests_properties(GuiSmokeTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(MemoryManagerSmokeTest
        tests/MemoryManagerSmokeTest.cpp
        ../resources/qt/resources.qrc
    )
    target_include_directories(MemoryManagerSmokeTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
    )
    target_link_libraries(MemoryManagerSmokeTest PRIVATE
        sdr9700_app_support
        Qt6::Test
    )
    add_test(NAME MemoryManagerSmokeTest COMMAND MemoryManagerSmokeTest)
    set_tests_properties(MemoryManagerSmokeTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(SystemStatsTest tests/SystemStatsTest.cpp)
    target_link_libraries(SystemStatsTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME SystemStatsTest COMMAND SystemStatsTest)

    add_executable(AutomationServerTest tests/AutomationServerTest.cpp)
    target_link_libraries(AutomationServerTest PRIVATE sdr9700_core Qt6::Test)
    add_test(NAME AutomationServerTest COMMAND AutomationServerTest)

    add_executable(AutomationControllerTest tests/AutomationControllerTest.cpp)
    target_include_directories(AutomationControllerTest PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/gui
    )
    target_link_libraries(AutomationControllerTest PRIVATE sdr9700_app_support Qt6::Test)
    add_test(NAME AutomationControllerTest COMMAND AutomationControllerTest)
    set_tests_properties(AutomationControllerTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
