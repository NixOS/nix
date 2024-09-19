#!/bin/bash

testclass="FromYAMLTest"
testmethod="execYAMLTest"

if [ -z "$1" ]; then
	echo "Usage: $0 PathToYamlTestSuiteRepository"
	echo
	echo "This script processes test cases from the yaml-test-suite repository (https://github.com/yaml/yaml-test-suite) and converts them into gtest tests for 'builtins.fromYAML'."
	exit 1
fi

echo "/* Generated by $(basename "$0") */"
echo
echo "#pragma once"
echo

for f in "$1"/src/*.yaml; do
	testname="$(basename "${f}" .yaml)"
	echo "static constexpr std::string_view T_${testname} = R\"RAW("
	cat "${f}"
	case "${testname}" in
		"4ABK")
			cat << EOL
  json: |
    {
      "unquoted": "separate",
      "http://foo.com": null,
      "omitted value": null
    }
EOL
			;;
		"SM9W")
			# not JSON compatible due to null key
			echo "  fail: true"
			;;
		*)
			;;
	esac
	echo ")RAW\";"
	echo
done

echo "namespace nix {"
for f in "$1"/src/*.yaml; do
	testname="$(basename "${f}" .yaml)"
	ignore="false"
	skip="false"
	case "${testname}" in
		"H7TQ"|"MUS6"|"ZYU8")
			echo "/** This test is ignored because these tests are not required to fail and rapidyaml ignores the YAML version string."
			ignore="true"
			;;
		"3HFZ"|"4EJS"|"5TRB"|"5U3A"|"7LBH"|"9C9N"|"9MQT"|"CVW2"|"CXX2"|"D49Q"|"DK4H"|"DK95"|"G5U8"|"JKF3"|"N782"|"QB6E"|"QLJ7"|"RXY3"|"S4GJ"|"S98Z"|"SY6V"|"VJP3"|"X4QW"|"Y79Y"|"YJV2"|"ZCZ6"|"ZL4Z"|"ZXT5"|"3HFZ"|"4EJS"|"5TRB"|"5U3A"|"7LBH"|"9C9N"|"9MQT"|"CVW2"|"CXX2"|"D49Q"|"DK4H"|"DK95"|"G5U8"|"JKF3"|"N782"|"QB6E"|"QLJ7"|"RXY3"|"S4GJ"|"S98Z"|"SY6V"|"VJP3"|"X4QW"|"Y79Y"|"YJV2"|"ZCZ6"|"ZL4Z"|"ZXT5")
			skip="true"
			;;
	esac
	echo "TEST_F(${testclass}, T_${testname})"
	echo "{"
	if [ "${testname}" = "565N" ]; then
		echo "    ASSERT_THROW(${testmethod}(T_${testname}), EvalError); // nix has no binary data type"
	else
		if [ "${skip}" = "true" ]; then
			echo "    GTEST_SKIP() << \"Reason: Invalid yaml is parsed successfully\";"
		fi
		echo "    ${testmethod}(T_${testname});"
	fi
	echo "}"
	[[ "${ignore}" = "true" ]] && echo "*/"
	echo
done
echo "} /* namespace nix */"
