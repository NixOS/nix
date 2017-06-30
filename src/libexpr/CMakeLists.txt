# Possible alternative notation:
BISON_TARGET(Parser parser.y ${CMAKE_CURRENT_SOURCE_DIR}/parser-tab.cc
    #COMPILE_FLAGS -d 
    DEFINES_FILE ${CMAKE_CURRENT_SOURCE_DIR}/parser-tab.hh)
FLEX_TARGET(Lexer lexer.l ${CMAKE_CURRENT_SOURCE_DIR}/lexer-tab.cc
    DEFINES_FILE ${CMAKE_CURRENT_SOURCE_DIR}/lexer-tab.hh)
ADD_FLEX_BISON_DEPENDENCY(Lexer Parser)

# set(BisonOutput ${CMAKE_CURRENT_SOURCE_DIR}/parser-tab.cc)
# add_custom_command(
#     OUTPUT ${BisonOutput}
#     COMMAND ${BISON_EXECUTABLE} -v
#           --defines=${CMAKE_CURRENT_SOURCE_DIR}/parser-tab.hh
#           -o ${BisonOutput}
#           ${CMAKE_CURRENT_SOURCE_DIR}/parser.y -d
#     COMMENT "Generating parser-tab.cc"
# )

# SET(FlexOutput ${CMAKE_CURRENT_SOURCE_DIR}/lexer-tab.cc)
# ADD_CUSTOM_COMMAND(
#     OUTPUT ${FlexOutput}
#     COMMAND ${FLEX_EXECUTABLE}
#           --outfile=${FlexOutput}
#           --header-file=${CMAKE_CURRENT_SOURCE_DIR}/lexer-tab.hh
#           ${CMAKE_CURRENT_SOURCE_DIR}/lexer.l
#     COMMENT "Generating lexer-tab.cc"
# )

nix_add_library(nixexpr SHARED
    attr-path.cc
    parser-tab.cc
    json-to-value.cc
    get-drvs.cc
    nixexpr.cc
    eval.cc
    value-to-json.cc
    names.cc
    attr-set.cc
    value-to-xml.cc
    primops.cc
    common-opts.cc
    primops/fetchgit.cc
    lexer-tab.cc
    parser-tab.cc)

target_link_libraries(nixexpr
    nixutil nixstore nixformat
    ${BDW_GC_LIBRARIES} # TODO: propagate??
    dl
)

nix_install(TARGETS nixexpr DESTINATION ${CMAKE_INSTALL_LIBDIR})

# The dependency on libgc must be propagated (i.e. meaning that
# programs/libraries that use libexpr must explicitly pass -lgc),
# because inline functions in libexpr's header files call libgc.
# libexpr_LDFLAGS_PROPAGATED = $(BDW_GC_LIBS)

configure_file(nix-expr.pc.in nix-expr.pc @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/nix-expr.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
        PERMISSIONS OWNER_READ OWNER_WRITE GROUP_READ WORLD_READ # 0644
)
