# aws s3 Dependency File
#============================================================================

#--------------------------------------------------
# FIXME: aws-cpp-sdk should be an optional dependency Unfortunately, importing
# it using the pkg-config option, which could be optional, fails because of
# the cflags in the pkg-config. They contain "-fno-exceptions -std=c++11",
# both of which break existing Nix code.
#--------------------------------------------------

# Look for aws-cpp-sdk
#--------------------------------------------------
aws_sdk_cpp_core_dep = declare_dependency(
    dependencies : cpp.find_library('aws-cpp-sdk-core',
        dirs : get_option('aws_sdk_cpp_lib_dir')),
    include_directories : include_directories(
        get_option('aws_sdk_cpp_include_dir')))

aws_sdk_cpp_s3_dep = declare_dependency(
    dependencies : cpp.find_library('aws-cpp-sdk-s3',
        dirs : get_option('aws_sdk_cpp_lib_dir')),
    include_directories : include_directories(
        get_option('aws_sdk_cpp_include_dir')))

aws_sdk_cpp_transfer_dep = declare_dependency(
    dependencies : cpp.find_library('aws-cpp-sdk-transfer',
        dirs : get_option('aws_sdk_cpp_lib_dir')),
    include_directories : include_directories(
        get_option('aws_sdk_cpp_include_dir')))


aws_sdk_cpp_dep = [
    aws_sdk_cpp_core_dep,
    aws_sdk_cpp_s3_dep,
    aws_sdk_cpp_transfer_dep,
]

# check if dependencies are found
#--------------------------------------------------
have_aws_sdk_cpp = true
foreach dep : aws_sdk_cpp_dep
    if not dep.found()
        have_aws_sdk_cpp = false
    endif
endforeach


# set config variable(s)
#--------------------------------------------------
config_h.set(
    'ENABLE_S3',
    have_aws_sdk_cpp.to_int(),
    description : 'Whether to enable S3 support via aws-sdk-cpp.')

if have_aws_sdk_cpp
    check_s3client = cpp.check_header(
        'aws/s3/S3Client.h',
        dependencies: aws_sdk_cpp_dep)

    if check_s3client
        config_h.set(
            'HAVE_AWS_S3_S3CLIENT_H', 1,
            description : 'Whether to enable S3 support via aws-sdk-cpp.')
    endif

    aws_version = meson.get_compiler('cpp').get_define(
        'AWS_SDK_VERSION_STRING',
        prefix : '#include <aws/core/VersionConfig.h>'
        ).strip('"').split('.')

    config_h.set(
        'AWS_VERSION_MAJOR', aws_version[0],
        description : 'Major version of aws-sdk-cpp.')

    config_h.set(
        'AWS_VERSION_MINOR', aws_version[1],
        description : 'Minor version of aws-sdk-cpp.')
    config_h.set(
        'AWS_VERSION_PATCH', aws_version[2],
        description : 'Patch version of aws-sdk-cpp.')

endif
