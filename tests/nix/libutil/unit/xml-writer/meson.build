# libutil.test.xml-writer
# ============================================================================

# build test
#---------------------------------------------------
libutil_xml_writer_test = executable(
    'libutil-xml-writer-test',
    install : false,
    sources : 'xml-writer.cc',
    include_directories : proj_inc,
    dependencies : libutil_tests_dep_list)

test(
    'libutil-xml-writer-test',
    libutil_xml_writer_test,
    protocol : 'gtest',
    suite : ['unit', 'libutil'])