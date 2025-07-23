#pragma once
// SPDX-FileCopyrightText: 2014 Emil Eriksson
//
// SPDX-License-Identifier: BSD-2-Clause
//
// The lion's share of this code is copy pasted directly out of RapidCheck
// headers, so the copyright is set accordingly.
/**
 * @file
 *
 * Implements the ability to run a RapidCheck test under gtest with changed
 * test parameters such as the number of tests to run. This is useful for
 * running very large numbers of the extremely cheap property tests.
 */

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>
#include <rapidcheck/gen/Arbitrary.hpp>

namespace rc::detail {

using MakeTestParams = TestParams (*)();

template<typename Testable>
void checkGTestWith(Testable && testable, MakeTestParams makeTestParams)
{
    const auto testInfo = ::testing::UnitTest::GetInstance()->current_test_info();
    detail::TestMetadata metadata;
    metadata.id = std::string(testInfo->test_case_name()) + "/" + std::string(testInfo->name());
    metadata.description = std::string(testInfo->name());

    const auto result = checkTestable(std::forward<Testable>(testable), metadata, makeTestParams());

    if (result.template is<detail::SuccessResult>()) {
        const auto success = result.template get<detail::SuccessResult>();
        if (!success.distribution.empty()) {
            printResultMessage(result, std::cout);
            std::cout << std::endl;
        }
    } else {
        std::ostringstream ss;
        printResultMessage(result, ss);
        throw std::runtime_error(ss.str());
    }
}
} // namespace rc::detail

#define RC_GTEST_PROP_WITH_PARAMS(TestCase, Name, MakeParams, ArgList)                      \
    void rapidCheck_propImpl_##TestCase##_##Name ArgList;                                   \
                                                                                            \
    TEST(TestCase, Name)                                                                    \
    {                                                                                       \
        ::rc::detail::checkGTestWith(&rapidCheck_propImpl_##TestCase##_##Name, MakeParams); \
    }                                                                                       \
                                                                                            \
    void rapidCheck_propImpl_##TestCase##_##Name ArgList
