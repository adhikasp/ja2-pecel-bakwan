file(GLOB LOCAL_JA2_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)
set(LOCAL_JA2_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/Automation.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/AutomationLua.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/AutomationServer.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/AutomationSession.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/BattleScenario.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/CampaignScenario.cc
    ${CMAKE_CURRENT_SOURCE_DIR}/ScenarioItems.cc
)

if (WITH_UNITTESTS)
    set(LOCAL_JA2_SOURCES
        ${LOCAL_JA2_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/Automation_unittest.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/CampaignScenario_unittest.cc
    )
endif()

set(JA2_SOURCES
    ${JA2_SOURCES}
    ${LOCAL_JA2_HEADERS}
    ${LOCAL_JA2_SOURCES}
    PARENT_SCOPE
)
