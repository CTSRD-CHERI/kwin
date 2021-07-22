set(SCENE_QPAINTER_BACKEND_SRCS
    platformqpaintersurfacetexture.cpp
    platformqpaintersurfacetexture_internal.cpp
    qpainterbackend.cpp
)
if (KWIN_BUILD_WAYLAND)
  list(APPEND SCENE_QPAINTER_BACKEND_SRCS platformqpaintersurfacetexture_wayland.cpp)
endif()

include(ECMQtDeclareLoggingCategory)
ecm_qt_declare_logging_category(SCENE_QPAINTER_BACKEND_SRCS
    HEADER
        logging.h
    IDENTIFIER
        KWIN_QPAINTER
    CATEGORY_NAME
        kwin_scene_qpainter
    DEFAULT_SEVERITY
        Critical
)

add_library(SceneQPainterBackend STATIC ${SCENE_QPAINTER_BACKEND_SRCS})
target_link_libraries(SceneQPainterBackend Qt::Core Qt::Widgets KF5::CoreAddons KF5::ConfigCore KF5::WindowSystem)
if (KWIN_BUILD_WAYLAND)
  target_link_libraries(SceneQPainterBackend Plasma::KWaylandServer)
endif()
target_include_directories(SceneQPainterBackend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/src)
