#!/bin/bash

testclass="FromYAMLTest"
testmethod="execYAMLTest"

if [ -z "$1" ]; then
	echo Usage: $0 PathToYamlTestSuiteRepository
	echo yaml-test-suite repository: https://github.com/yaml/yaml-test-suite
	exit 1
fi

echo "#pragma once"
echo
echo

for f in "$1"/src/*.yaml; do
	testname="$(basename ${f} .yaml)"
	echo "static constexpr std::string_view T_${testname} = R\"RAW("
	cat ${f}
	echo ")RAW\";"
	echo
done

echo
echo
echo "namespace nix {"
for f in "$1"/src/*.yaml; do
	testname="$(basename ${f} .yaml)"
	echo "    TEST_F(${testclass}, T_${testname}) {"
	if [ "${testname}" = "Y79Y" ]; then
		echo "        GTEST_SKIP(); // bug in ryml"
	fi
	if [ "${testname}" = "565N" ]; then
		echo "        ASSERT_THROW(${testmethod}(T_${testname}),EvalError); // nix has no binary data type"
	else
		echo "        ASSERT_EQ(${testmethod}(T_${testname}),\"OK\");"
	fi
	echo "    }"
	echo
done
echo "} /* namespace nix */"
