file(GLOB LOCAL_JA2_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
set(LOCAL_JA2_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/AttachmentRules.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/DamagePipeline.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/EquipmentCatalog.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/Lbe.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/LoadoutBuilder.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/LoadoutModel.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/PocketRules.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/Slots.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/Stash.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/WeaponBallistics.cc
)

if (WITH_UNITTESTS)
    set(LOCAL_JA2_SOURCES
        ${LOCAL_JA2_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/DamagePipeline_unittest.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/Equipment_unittest.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/LoadoutBuilder_unittest.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/LoadoutModel_unittest.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/Stash_unittest.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/WeaponBallistics_unittest.cc
    )
endif()

set(JA2_SOURCES
    ${JA2_SOURCES}
    ${LOCAL_JA2_HEADERS}
    ${LOCAL_JA2_SOURCES}
    PARENT_SCOPE
)
