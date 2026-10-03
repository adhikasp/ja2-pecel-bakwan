file(GLOB LOCAL_JA2_HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

set(LOCAL_JA2_SOURCES "")
if (WITH_UNITTESTS)
    set(LOCAL_JA2_SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/Containers_unittests.cc
        ${CMAKE_CURRENT_SOURCE_DIR}/Views_unittests.cc
    )
endif()

set_property(
    SOURCE ${LOCAL_JA2_SOURCES}
    DIRECTORY ${CMAKE_SOURCE_DIR}
    PROPERTY UNITY_GROUP "externalized"
)

# Containers_unittests.cc and Views_unittests.cc each define their own global TestEntity, so one
# of them has to stay out of the shared batch
set_property(
    SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/Views_unittests.cc
    DIRECTORY ${CMAKE_SOURCE_DIR}
    PROPERTY SKIP_UNITY_BUILD_INCLUSION ON
)

set(JA2_SOURCES
    ${JA2_SOURCES}
    ${LOCAL_JA2_HEADERS}
    ${LOCAL_JA2_SOURCES}
    PARENT_SCOPE
)
