# aws s3 dependency file
#============================================================================


# Look for aws-cpp-sdk, an optional dependency
#--------------------------------------------------
if get_option('with_s3')

    has_s3client = meson.get_compiler('cpp').check_header('aws/s3/S3Client.h')

    aws_version = meson.get_compiler('cpp').get_define(
        'AWS_SDK_VERSION_STRING',
        prefix : '#include <aws/core/VersionConfig.h>'
    ).strip('"').split('.')

    aws_sdk_cpp_dep = declare_dependency(
        dependencies : [
            dependency('aws-cpp-sdk-core'),
            dependency('aws-cpp-sdk-s3'),
            dependency('aws-cpp-sdk-transfer')])

    if has_s3client
        config_h.set(
            'HAVE_AWS_S3_S3CLIENT_H', 1,
            description : 'Whether to enable S3 support via aws-sdk-cpp.')
    endif

    config_h.set(
        'AWS_VERSION_MAJOR', aws_version[0],
        description : 'Major version of aws-sdk-cpp.')

    config_h.set(
        'AWS_VERSION_MINOR', aws_version[1],
        description : 'Minor version of aws-sdk-cpp.')
    config_h.set(
        'AWS_VERSION_PATCH', aws_version[2],
        description : 'Patch version of aws-sdk-cpp.')

else
    aws_sdk_cpp_dep = dependency('', required: false)
    config_h.set(
        'ENABLE_S3', 0,
        description : 'Whether to enable S3 support via aws-sdk-cpp.')
endif
