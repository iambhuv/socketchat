find_path(sodium_INCLUDE_DIR
    NAMES sodium.h
)

find_library(sodium_LIBRARY
    NAMES sodium libsodium
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(sodium
    REQUIRED_VARS sodium_LIBRARY sodium_INCLUDE_DIR
)

if(sodium_FOUND AND NOT TARGET sodium::sodium)
    add_library(sodium::sodium UNKNOWN IMPORTED)
    set_target_properties(sodium::sodium PROPERTIES
        IMPORTED_LOCATION "${sodium_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${sodium_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(sodium_INCLUDE_DIR sodium_LIBRARY)
