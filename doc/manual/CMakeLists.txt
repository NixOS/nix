set(doc_generate_nonet YES CACHE BOOL "")
message(STATUS "doc_generate_nonet: ${doc_generate_nonet}")
if(${doc_generate_nonet} STREQUAL YES)
    set(nonet_flag "--nonet")
    set(docbookxsl /usr/share/xml/docbook/stylesheet/docbook-xsl)
    set(docbookrng  ${docbookxsl}/slides/schema/relaxng/docbook.rng)
else()
    set(docbookrng "http://docbook.org/xml/5.0/rng/docbook.rng")
    set(docbookxsl "http://docbook.sourceforge.net/release/xsl-ns/current")
endif()
set(profile_xsl ${docbookxsl}/profiling/profile.xsl)
set(docbook_xsl ${docbookxsl}/manpages/docbook.xsl)
set(manpages_xsl ${docbookxsl}/xhtml/docbook.xsl)

message(STATUS "docbookrng: ${docbookrng}")
message(STATUS "docbookxsl: ${docbookxsl}")
message(STATUS "profile_xsl: ${profile_xsl}")
message(STATUS "docbook_xsl: ${docbook_xsl}")


set(docdir ${CMAKE_INSTALL_DOCDIR})
set(mandir ${CMAKE_INSTALL_MANDIR})

set(XSLTPROC ${xsltproc}
    ${nonet_flag}
    ${xmlflags}
    --param section.autolabel 1
    --param section.label.includes.component.label 1
    --param html.stylesheet \\'style.css\\'
    --param xref.with.number.and.title 1
    --param toc.section.depth 3
    --param admon.style \\'\\'
    --param callout.graphics.extension \\'.gif\\'
    --param contrib.inline.enabled 0
    --stringparam generate.toc \"book toc\" # don't escape this
    --param keep.relative.image.uris 0
)

#file(GLOB MANUAL_SRCS *.xml)
set(MANUAL_SRCS # ensure manual.xml first
    manual.xml
    hacking.xml
    nix-lang-ref.xml
    schemas.xml
)
function(prepend_path _path _srcs)
    unset(_tmp)
    foreach(src ${${_srcs}})
        list(APPEND _tmp ${_path}/${src})
    endforeach()
    set(${_srcs} ${_tmp} PARENT_SCOPE)
endfunction()

prepend_path(${CMAKE_CURRENT_SOURCE_DIR} MANUAL_SRCS)

#message(STATUS "xmllint: ${xmllint}")
#message(STATUS "XSLTPROC: ${XSLTPROC}")
#message(STATUS "docbookrng: ${docbookrng}")
list(GET MANUAL_SRCS 0 MANUAL_SRC)

function(nix_generate_manual_xmli _target _depends _version)
    set(_fullname ${CMAKE_CURRENT_BINARY_DIR}/${_target})
    #message(STATUS "nix_generate_manual_xmli: ${_target}\n${_depends}")
    add_custom_command(
        OUTPUT ${_fullname}
        COMMAND ${xmllint} ${nonet_flag} --xinclude ${_depends} --output ${_fullname}
        DEPENDS ${_depends} ${_version}
    )
endfunction()

# Do XInclude processing / RelaxNG validation
nix_generate_manual_xmli(manual.xmli
    ${MANUAL_SRC}
    ${CMAKE_CURRENT_SOURCE_DIR}/version.txt
)

function(nix_generate_version_txt _target)
    set(_fullname ${CMAKE_CURRENT_SOURCE_DIR}/${_target})
    add_custom_command(
        OUTPUT ${_fullname}
        COMMAND echo -n "${PACKAGE_VERSION}" > ${_fullname}
        VERBATIM
    )
endfunction()
nix_generate_version_txt(version.txt)

# Note: RelaxNG validation requires xmllint >= 2.7.4.
function(nix_is_manual_valid _target _depends)
    set(_fullname ${CMAKE_CURRENT_BINARY_DIR}/${_target})
    add_custom_command(
        OUTPUT ${_fullname}
        DEPENDS ${_depends}
        COMMAND ${XSLTPROC} --novalid --stringparam profile.condition manual
                ${profile_xsl} ${_depends} 2> /dev/null |
                ${xmllint} ${nonet_flag} --noout --relaxng ${docbookrng} -
                && touch ${_fullname}
    )
endfunction()
nix_is_manual_valid(manual.is-valid ${CMAKE_CURRENT_BINARY_DIR}/manual.xmli)

# Generate man pages.
set(man_pages 
    nix-env.1
    nix-build.1
    nix-shell.1
    nix-store.1
    nix-instantiate.1
    nix-collect-garbage.1
    nix-push.1
    nix-pull.1
    nix-prefetch-url.1
    nix-channel.1
    nix-install-package.1
    nix-hash.1
    nix-copy-closure.1
    nix.conf.5
    nix-daemon.8
)

function(nix_generate_manual _target _depends)
    set(_fullname ${CMAKE_CURRENT_BINARY_DIR}/${_target})
    list(GET _depends 0 first_depend)
    add_custom_command(
        OUTPUT ${_fullname}
        DEPENDS ${_depends}
        COMMAND ${XSLTPROC} --novalid --stringparam profile.condition manpage
            ${profile_xsl} ${first_depend} 2> /dev/null |
            (cd ${CMAKE_CURRENT_BINARY_DIR} && ${XSLTPROC} ${docbook_xsl} -)
    )
    add_custom_target(dummy_${_target} ALL
        DEPENDS ${_fullname}
    )
endfunction()
nix_generate_manual(nix-env.1 "${CMAKE_CURRENT_BINARY_DIR}/manual.xmli"
    #"{CMAKE_CURRENT_BINARY_DIR}/manual.is-valid"
)

function(shortest_extension _file _ext)
    string(REGEX MATCH "\\.([^.]+)$" dummy ${_file})
    set(${_ext} ${CMAKE_MATCH_1} PARENT_SCOPE)
endfunction()

foreach(man_page ${man_pages})
    unset(NUM_EXT)
    shortest_extension(${man_page} NUM_EXT)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${man_page}
        DESTINATION ${mandir}/man${NUM_EXT}/)
endforeach()

list(REMOVE_AT man_pages 0)
foreach(man_page ${man_pages})
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${man_page}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/nix-env.1)
    add_custom_target(dummy_${man_page} ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${man_page}
    )
endforeach()


# Generate the HTML manual.
function(nix_generate_manual_html _target _depends)
    set(_fullname ${CMAKE_CURRENT_BINARY_DIR}/${_target})
    list(GET _depends 0 first_depend)
    add_custom_command(
        OUTPUT ${_fullname}
        DEPENDS ${_depends}
        COMMAND ${XSLTPROC} --xinclude --stringparam profile.condition manual
            ${profile_xsl} ${first_depend} 2> /dev/null |
            ${XSLTPROC} --output ${_fullname} ${manpages_xsl} -
    )
    add_custom_target(dummy_${_target} ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${_target}
    )
endfunction()
nix_generate_manual_html(manual.html ${MANUAL_SRCS} ${CMAKE_CURRENT_BINARY_DIR}/manual.is-valid)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/manual.html style.css DESTINATION ${docdir}/manual)

file(GLOB figures-png figures/*.png)
install(FILES ${figures-png} DESTINATION ${docdir}/manual/figures)

file(GLOB images-callouts-gif images/callouts/*.gif)
install(FILES ${images-callouts-gif} DESTINATION ${docdir}/manual/images/callouts)

function(nix_symlink file link)
    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${link}
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${file}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMAND ln -f -s ${file} ${link}
    )
    add_custom_target(dummy_${link} ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${link}
    )
endfunction()

nix_symlink(manual.html index.html)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/index.html
    DESTINATION ${docdir}/manual)

# Generate the PDF manual.
function(nix_generate_manual_pdf _target _depends)
    set(_fullname ${CMAKE_CURRENT_BINARY_DIR}/${_target})
    list(GET _depends 0 first_depend)
    add_custom_command(
        OUTPUT ${_fullname}
        DEPENDS ${_depends}
        COMMAND
            if test -n \"${dblatex}\" \; then
                ${XSLTPROC} --xinclude --stringparam profile.condition manual
                  ${profile_xsl} ${first_depend} |
                  ${dblatex} -o ${_target} ${dblatex_opts} - \;
            else
                echo \"Please install dblatex and rerun configure.\"\;
                exit 1\;
            fi
    )
    add_custom_target(dummy_${_target} ALL
        DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${_target}
    )
endfunction()
nix_generate_manual_pdf(manual.pdf ${MANUAL_SRCS} ${CMAKE_CURRENT_BINARY_DIR}/manual.is-valid)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/manual.pdf DESTINATION ${docdir}/manual)
