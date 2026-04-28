# Locate the curses panel library when pkg-config is unavailable.

find_library(CURSES_PANEL_LIBRARY
    NAMES panelw panel pdcurses_panel
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(CursesPanel
    REQUIRED_VARS CURSES_PANEL_LIBRARY
)

if(CursesPanel_FOUND AND NOT TARGET Curses::Panel)
    add_library(Curses::Panel UNKNOWN IMPORTED)
    set_target_properties(Curses::Panel PROPERTIES
        IMPORTED_LOCATION "${CURSES_PANEL_LIBRARY}"
    )
endif()
