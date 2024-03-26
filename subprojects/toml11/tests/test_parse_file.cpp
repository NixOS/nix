#include <toml.hpp>

#include "unit_test.hpp"

#include <deque>
#include <fstream>
#include <iostream>
#include <map>

BOOST_AUTO_TEST_CASE(test_example)
{
    const auto data = toml::parse(testinput("example.toml"));

    BOOST_TEST(toml::find<std::string>(data, "title") == "TOML Example");
    const auto& owner = toml::find(data, "owner");
    {
        BOOST_TEST(toml::find<std::string>(owner, "name") == "Tom Preston-Werner");
        BOOST_TEST(toml::find<std::string>(owner, "organization") == "GitHub");
        BOOST_TEST(toml::find<std::string>(owner, "bio") ==
                          "GitHub Cofounder & CEO\nLikes tater tots and beer.");
        BOOST_TEST(toml::find<toml::offset_datetime>(owner, "dob") ==
                          toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                                                toml::local_time(7, 32, 0), toml::time_offset(0, 0)));
    }

    const auto& database = toml::find(data, "database");
    {
        BOOST_TEST(toml::find<std::string>(database, "server") == "192.168.1.1");
        const std::vector<int> expected_ports{8001, 8001, 8002};
        BOOST_CHECK(toml::find<std::vector<int>>(database, "ports") == expected_ports);
        BOOST_TEST(toml::find<int >(database, "connection_max") == 5000);
        BOOST_TEST(toml::find<bool>(database, "enabled") == true);
    }

    const auto& servers = toml::find(data, "servers");
    {
        toml::table alpha = toml::find<toml::table>(servers, "alpha");
        BOOST_TEST(toml::get<std::string>(alpha.at("ip")) == "10.0.0.1");
        BOOST_TEST(toml::get<std::string>(alpha.at("dc")) == "eqdc10");

        toml::table beta = toml::find<toml::table>(servers, "beta");
        BOOST_TEST(toml::get<std::string>(beta.at("ip")) == "10.0.0.2");
        BOOST_TEST(toml::get<std::string>(beta.at("dc")) == "eqdc10");
        BOOST_TEST(toml::get<std::string>(beta.at("country")) == "\xE4\xB8\xAD\xE5\x9B\xBD");
    }

    const auto& clients = toml::find(data, "clients");
    {
        toml::array clients_data = toml::find<toml::array>(clients, "data");

        std::vector<std::string> expected_name{"gamma", "delta"};
        BOOST_CHECK(toml::get<std::vector<std::string>>(clients_data.at(0)) == expected_name);

        std::vector<int> expected_number{1, 2};
        BOOST_CHECK(toml::get<std::vector<int>>(clients_data.at(1)) == expected_number);

        std::vector<std::string> expected_hosts{"alpha", "omega"};
        BOOST_CHECK(toml::find<std::vector<std::string>>(clients, "hosts") == expected_hosts);
    }

    std::vector<toml::table> products =
        toml::find<std::vector<toml::table>>(data, "products");
    {
        BOOST_TEST(toml::get<std::string>(products.at(0).at("name")) == "Hammer");
        BOOST_TEST(toml::get<std::int64_t>(products.at(0).at("sku")) == 738594937);

        BOOST_TEST(toml::get<std::string>(products.at(1).at("name")) == "Nail");
        BOOST_TEST(toml::get<std::int64_t>(products.at(1).at("sku")) == 284758393);
        BOOST_TEST(toml::get<std::string>(products.at(1).at("color")) == "gray");
    }
}

BOOST_AUTO_TEST_CASE(test_example_stream)
{
    std::ifstream ifs(testinput("example.toml"), std::ios::binary);
    const auto data = toml::parse(ifs);

    BOOST_TEST(toml::find<std::string>(data, "title") == "TOML Example");
    const auto& owner = toml::find(data, "owner");
    {
        BOOST_TEST(toml::find<std::string>(owner, "name") == "Tom Preston-Werner");
        BOOST_TEST(toml::find<std::string>(owner, "organization") == "GitHub");
        BOOST_TEST(toml::find<std::string>(owner, "bio") ==
                          "GitHub Cofounder & CEO\nLikes tater tots and beer.");
        BOOST_TEST(toml::find<toml::offset_datetime>(owner, "dob") ==
                          toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                                                toml::local_time(7, 32, 0), toml::time_offset(0, 0)));
    }

    const auto& database = toml::find(data, "database");
    {
        BOOST_TEST(toml::find<std::string>(database, "server") == "192.168.1.1");
        const std::vector<int> expected_ports{8001, 8001, 8002};
        BOOST_CHECK(toml::find<std::vector<int>>(database, "ports") == expected_ports);
        BOOST_TEST(toml::find<int >(database, "connection_max") == 5000);
        BOOST_TEST(toml::find<bool>(database, "enabled") == true);
    }

    const auto& servers = toml::find(data, "servers");
    {
        toml::table alpha = toml::find<toml::table>(servers, "alpha");
        BOOST_TEST(toml::get<std::string>(alpha.at("ip")) == "10.0.0.1");
        BOOST_TEST(toml::get<std::string>(alpha.at("dc")) == "eqdc10");

        toml::table beta = toml::find<toml::table>(servers, "beta");
        BOOST_TEST(toml::get<std::string>(beta.at("ip")) == "10.0.0.2");
        BOOST_TEST(toml::get<std::string>(beta.at("dc")) == "eqdc10");
        BOOST_TEST(toml::get<std::string>(beta.at("country")) == "\xE4\xB8\xAD\xE5\x9B\xBD");
    }

    const auto& clients = toml::find(data, "clients");
    {
        toml::array clients_data = toml::find<toml::array>(clients, "data");
        std::vector<std::string> expected_name{"gamma", "delta"};
        BOOST_CHECK(toml::get<std::vector<std::string>>(clients_data.at(0)) == expected_name);

        std::vector<int> expected_number{1, 2};
        BOOST_CHECK(toml::get<std::vector<int>>(clients_data.at(1)) == expected_number);

        std::vector<std::string> expected_hosts{"alpha", "omega"};
        BOOST_CHECK(toml::find<std::vector<std::string>>(clients, "hosts") == expected_hosts);
    }

    std::vector<toml::table> products =
        toml::find<std::vector<toml::table>>(data, "products");
    {
        BOOST_TEST(toml::get<std::string>(products.at(0).at("name")) ==
                          "Hammer");
        BOOST_TEST(toml::get<std::int64_t>(products.at(0).at("sku")) ==
                          738594937);

        BOOST_TEST(toml::get<std::string>(products.at(1).at("name")) ==
                          "Nail");
        BOOST_TEST(toml::get<std::int64_t>(products.at(1).at("sku")) ==
                          284758393);
        BOOST_TEST(toml::get<std::string>(products.at(1).at("color")) ==
                          "gray");
    }
}

BOOST_AUTO_TEST_CASE(test_example_file_pointer)
{
    FILE * file = fopen(testinput("example.toml").c_str(), "rb");
    const auto data = toml::parse(file, "toml/tests/example.toml");
    fclose(file);

    BOOST_TEST(toml::find<std::string>(data, "title") == "TOML Example");
    const auto& owner = toml::find(data, "owner");
    {
        BOOST_TEST(toml::find<std::string>(owner, "name") == "Tom Preston-Werner");
        BOOST_TEST(toml::find<std::string>(owner, "organization") == "GitHub");
        BOOST_TEST(toml::find<std::string>(owner, "bio") ==
                          "GitHub Cofounder & CEO\nLikes tater tots and beer.");
        BOOST_TEST(toml::find<toml::offset_datetime>(owner, "dob") ==
                          toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                                                toml::local_time(7, 32, 0), toml::time_offset(0, 0)));
    }

    const auto& database = toml::find(data, "database");
    {
        BOOST_TEST(toml::find<std::string>(database, "server") == "192.168.1.1");
        const std::vector<int> expected_ports{8001, 8001, 8002};
        BOOST_CHECK(toml::find<std::vector<int>>(database, "ports") == expected_ports);
        BOOST_TEST(toml::find<int >(database, "connection_max") == 5000);
        BOOST_TEST(toml::find<bool>(database, "enabled") == true);
    }

    const auto& servers = toml::find(data, "servers");
    {
        toml::table alpha = toml::find<toml::table>(servers, "alpha");
        BOOST_TEST(toml::get<std::string>(alpha.at("ip")) == "10.0.0.1");
        BOOST_TEST(toml::get<std::string>(alpha.at("dc")) == "eqdc10");

        toml::table beta = toml::find<toml::table>(servers, "beta");
        BOOST_TEST(toml::get<std::string>(beta.at("ip")) == "10.0.0.2");
        BOOST_TEST(toml::get<std::string>(beta.at("dc")) == "eqdc10");
        BOOST_TEST(toml::get<std::string>(beta.at("country")) == "\xE4\xB8\xAD\xE5\x9B\xBD");
    }

    const auto& clients = toml::find(data, "clients");
    {
        toml::array clients_data = toml::find<toml::array>(clients, "data");
        std::vector<std::string> expected_name{"gamma", "delta"};
        BOOST_CHECK(toml::get<std::vector<std::string>>(clients_data.at(0)) == expected_name);

        std::vector<int> expected_number{1, 2};
        BOOST_CHECK(toml::get<std::vector<int>>(clients_data.at(1)) == expected_number);

        std::vector<std::string> expected_hosts{"alpha", "omega"};
        BOOST_CHECK(toml::find<std::vector<std::string>>(clients, "hosts") == expected_hosts);
    }

    std::vector<toml::table> products =
        toml::find<std::vector<toml::table>>(data, "products");
    {
        BOOST_TEST(toml::get<std::string>(products.at(0).at("name")) ==
                          "Hammer");
        BOOST_TEST(toml::get<std::int64_t>(products.at(0).at("sku")) ==
                          738594937);

        BOOST_TEST(toml::get<std::string>(products.at(1).at("name")) ==
                          "Nail");
        BOOST_TEST(toml::get<std::int64_t>(products.at(1).at("sku")) ==
                          284758393);
        BOOST_TEST(toml::get<std::string>(products.at(1).at("color")) ==
                          "gray");
    }
}

BOOST_AUTO_TEST_CASE(test_fruit)
{
    const auto data = toml::parse(testinput("fruit.toml"));
    const auto blah = toml::find<toml::array>(toml::find(data, "fruit"), "blah");
    BOOST_TEST(toml::find<std::string>(blah.at(0), "name") == "apple");
    BOOST_TEST(toml::find<std::string>(blah.at(1), "name") == "banana");
    {
        const auto physical = toml::find(blah.at(0), "physical");
        BOOST_TEST(toml::find<std::string>(physical, "color") == "red");
        BOOST_TEST(toml::find<std::string>(physical, "shape") == "round");
    }
    {
        const auto physical = toml::find(blah.at(1), "physical");
        BOOST_TEST(toml::find<std::string>(physical, "color") == "yellow");
        BOOST_TEST(toml::find<std::string>(physical, "shape") == "bent");
    }
}

BOOST_AUTO_TEST_CASE(test_hard_example)
{
    const auto data = toml::parse(testinput("hard_example.toml"));
    const auto the = toml::find(data, "the");
    BOOST_TEST(toml::find<std::string>(the, "test_string") ==
                      "You'll hate me after this - #");

    const auto hard = toml::find(the, "hard");
    const std::vector<std::string> expected_the_hard_test_array{"] ", " # "};
    BOOST_CHECK(toml::find<std::vector<std::string>>(hard, "test_array") ==
                expected_the_hard_test_array);
    const std::vector<std::string> expected_the_hard_test_array2{
        "Test #11 ]proved that", "Experiment #9 was a success"};
    BOOST_CHECK(toml::find<std::vector<std::string>>(hard, "test_array2") ==
                expected_the_hard_test_array2);
    BOOST_TEST(toml::find<std::string>(hard, "another_test_string") ==
                      " Same thing, but with a string #");
    BOOST_TEST(toml::find<std::string>(hard, "harder_test_string") ==
                      " And when \"'s are in the string, along with # \"");

    const auto bit = toml::find(hard, "bit#");
    BOOST_TEST(toml::find<std::string>(bit, "what?") ==
                      "You don't think some user won't do that?");
    const std::vector<std::string> expected_multi_line_array{"]"};
    BOOST_CHECK(toml::find<std::vector<std::string>>(bit, "multi_line_array") ==
                expected_multi_line_array);
}
BOOST_AUTO_TEST_CASE(test_hard_example_comment)
{
    const auto data = toml::parse<toml::preserve_comments>(testinput("hard_example.toml"));
    const auto the = toml::find(data, "the");
    BOOST_TEST(toml::find<std::string>(the, "test_string") ==
                      "You'll hate me after this - #");

    const auto hard = toml::find(the, "hard");
    const std::vector<std::string> expected_the_hard_test_array{"] ", " # "};
    BOOST_CHECK(toml::find<std::vector<std::string>>(hard, "test_array") ==
                expected_the_hard_test_array);
    const std::vector<std::string> expected_the_hard_test_array2{
        "Test #11 ]proved that", "Experiment #9 was a success"};
    BOOST_CHECK(toml::find<std::vector<std::string>>(hard, "test_array2") ==
                expected_the_hard_test_array2);
    BOOST_TEST(toml::find<std::string>(hard, "another_test_string") ==
                      " Same thing, but with a string #");
    BOOST_TEST(toml::find<std::string>(hard, "harder_test_string") ==
                      " And when \"'s are in the string, along with # \"");

    const auto bit = toml::find(hard, "bit#");
    BOOST_TEST(toml::find<std::string>(bit, "what?") ==
                      "You don't think some user won't do that?");
    const std::vector<std::string> expected_multi_line_array{"]"};
    BOOST_CHECK(toml::find<std::vector<std::string>>(bit, "multi_line_array") ==
                expected_multi_line_array);
}


BOOST_AUTO_TEST_CASE(test_example_preserve_comment)
{
    const auto data = toml::parse<toml::preserve_comments>(testinput("example.toml"));

    BOOST_TEST(toml::find<std::string>(data, "title") == "TOML Example");
    const auto& owner = toml::find(data, "owner");
    {
        BOOST_TEST(toml::find<std::string>(owner, "name") == "Tom Preston-Werner");
        BOOST_TEST(toml::find<std::string>(owner, "organization") == "GitHub");
        BOOST_TEST(toml::find<std::string>(owner, "bio") ==
                          "GitHub Cofounder & CEO\nLikes tater tots and beer.");
        BOOST_TEST(toml::find<toml::offset_datetime>(owner, "dob") ==
                          toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                                                toml::local_time(7, 32, 0), toml::time_offset(0, 0)));
        BOOST_TEST(toml::find(owner, "dob").comments().at(0) ==
                          " First class dates? Why not?");
    }

    const auto& database = toml::find(data, "database");
    {
        BOOST_TEST(toml::find<std::string>(database, "server") == "192.168.1.1");
        const std::vector<int> expected_ports{8001, 8001, 8002};
        BOOST_CHECK(toml::find<std::vector<int>>(database, "ports") == expected_ports);
        BOOST_TEST(toml::find<int >(database, "connection_max") == 5000);
        BOOST_TEST(toml::find<bool>(database, "enabled") == true);
    }

    const auto& servers = toml::find(data, "servers");
    {
        const auto& alpha = toml::find(servers, "alpha");
        BOOST_TEST(alpha.comments().at(0) ==
            " You can indent as you please. Tabs or spaces. TOML don't care.");
        BOOST_TEST(toml::find<std::string>(alpha, "ip") == "10.0.0.1");
        BOOST_TEST(toml::find<std::string>(alpha, "dc") == "eqdc10");

        const auto& beta = toml::find(servers, "beta");
        BOOST_TEST(toml::find<std::string>(beta, "ip") == "10.0.0.2");
        BOOST_TEST(toml::find<std::string>(beta, "dc") == "eqdc10");
        BOOST_TEST(toml::find<std::string>(beta, "country") ==
                          "\xE4\xB8\xAD\xE5\x9B\xBD");
        BOOST_TEST(toml::find(beta, "country").comments().at(0) ==
                          " This should be parsed as UTF-8");
    }

    const auto& clients = toml::find(data, "clients");
    {
        BOOST_TEST(toml::find(clients, "data").comments().at(0) ==
                " just an update to make sure parsers support it");


        toml::array clients_data = toml::find<toml::array>(clients, "data");
        std::vector<std::string> expected_name{"gamma", "delta"};
        BOOST_CHECK(toml::get<std::vector<std::string>>(clients_data.at(0)) ==
                    expected_name);
        std::vector<int> expected_number{1, 2};
        BOOST_CHECK(toml::get<std::vector<int>>(clients_data.at(1)) ==
                    expected_number);
        std::vector<std::string> expected_hosts{"alpha", "omega"};
        BOOST_CHECK(toml::find<std::vector<std::string>>(clients, "hosts") ==
                    expected_hosts);

        BOOST_TEST(toml::find(clients, "hosts").comments().at(0) ==
                    " Line breaks are OK when inside arrays");
    }

    std::vector<toml::table> products =
        toml::find<std::vector<toml::table>>(data, "products");
    {
        BOOST_TEST(toml::get<std::string>(products.at(0).at("name")) ==
                          "Hammer");
        BOOST_TEST(toml::get<std::int64_t>(products.at(0).at("sku")) ==
                          738594937);

        BOOST_TEST(toml::get<std::string>(products.at(1).at("name")) ==
                          "Nail");
        BOOST_TEST(toml::get<std::int64_t>(products.at(1).at("sku")) ==
                          284758393);
        BOOST_TEST(toml::get<std::string>(products.at(1).at("color")) ==
                          "gray");
    }
}

BOOST_AUTO_TEST_CASE(test_example_preserve_stdmap_stddeque)
{
    const auto data = toml::parse<toml::preserve_comments, std::map, std::deque>(
            testinput("example.toml"));

    static_assert(std::is_same<typename decltype(data)::table_type,
            std::map<toml::key, typename std::remove_cv<decltype(data)>::type>
            >::value, "");
    static_assert(std::is_same<typename decltype(data)::array_type,
            std::deque<typename std::remove_cv<decltype(data)>::type>
            >::value, "");

    BOOST_TEST(toml::find<std::string>(data, "title") == "TOML Example");
    const auto& owner = toml::find(data, "owner");
    {
        BOOST_TEST(toml::find<std::string>(owner, "name") == "Tom Preston-Werner");
        BOOST_TEST(toml::find<std::string>(owner, "organization") == "GitHub");
        BOOST_TEST(toml::find<std::string>(owner, "bio") ==
                          "GitHub Cofounder & CEO\nLikes tater tots and beer.");
        BOOST_TEST(toml::find<toml::offset_datetime>(owner, "dob") ==
                          toml::offset_datetime(toml::local_date(1979, toml::month_t::May, 27),
                                                toml::local_time(7, 32, 0), toml::time_offset(0, 0)));
        BOOST_TEST(toml::find(owner, "dob").comments().at(0) ==
                          " First class dates? Why not?");
    }

    const auto& database = toml::find(data, "database");
    {
        BOOST_TEST(toml::find<std::string>(database, "server") == "192.168.1.1");
        const std::vector<int> expected_ports{8001, 8001, 8002};
        BOOST_CHECK(toml::find<std::vector<int>>(database, "ports") == expected_ports);
        BOOST_TEST(toml::find<int >(database, "connection_max") == 5000);
        BOOST_TEST(toml::find<bool>(database, "enabled") == true);
    }

    const auto& servers = toml::find(data, "servers");
    {
        const auto& alpha = toml::find(servers, "alpha");
        BOOST_TEST(alpha.comments().at(0) ==
            " You can indent as you please. Tabs or spaces. TOML don't care.");
        BOOST_TEST(toml::find<std::string>(alpha, "ip") == "10.0.0.1");
        BOOST_TEST(toml::find<std::string>(alpha, "dc") == "eqdc10");

        const auto& beta = toml::find(servers, "beta");
        BOOST_TEST(toml::find<std::string>(beta, "ip") == "10.0.0.2");
        BOOST_TEST(toml::find<std::string>(beta, "dc") == "eqdc10");
        BOOST_TEST(toml::find<std::string>(beta, "country") ==
                          "\xE4\xB8\xAD\xE5\x9B\xBD");
        BOOST_TEST(toml::find(beta, "country").comments().at(0) ==
                          " This should be parsed as UTF-8");
    }

    const auto& clients = toml::find(data, "clients");
    {
        BOOST_TEST(toml::find(clients, "data").comments().at(0) ==
                " just an update to make sure parsers support it");


        toml::array clients_data = toml::find<toml::array>(clients, "data");
        std::vector<std::string> expected_name{"gamma", "delta"};
        BOOST_CHECK(toml::get<std::vector<std::string>>(clients_data.at(0)) ==
                    expected_name);
        std::vector<int> expected_number{1, 2};
        BOOST_CHECK(toml::get<std::vector<int>>(clients_data.at(1)) ==
                    expected_number);
        std::vector<std::string> expected_hosts{"alpha", "omega"};
        BOOST_CHECK(toml::find<std::vector<std::string>>(clients, "hosts") ==
                    expected_hosts);

        BOOST_TEST(toml::find(clients, "hosts").comments().at(0) ==
                    " Line breaks are OK when inside arrays");
    }

    std::vector<toml::table> products =
        toml::find<std::vector<toml::table>>(data, "products");
    {
        BOOST_TEST(toml::get<std::string>(products.at(0).at("name")) ==
                          "Hammer");
        BOOST_TEST(toml::get<std::int64_t>(products.at(0).at("sku")) ==
                          738594937);

        BOOST_TEST(toml::get<std::string>(products.at(1).at("name")) ==
                          "Nail");
        BOOST_TEST(toml::get<std::int64_t>(products.at(1).at("sku")) ==
                          284758393);
        BOOST_TEST(toml::get<std::string>(products.at(1).at("color")) ==
                          "gray");
    }
}

// ---------------------------------------------------------------------------
// after here, the test codes generate the content of a file.

BOOST_AUTO_TEST_CASE(test_file_with_BOM)
{
    {
        const std::string table(
            "\xEF\xBB\xBF" // BOM
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss, "test_file_with_BOM.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "\xEF\xBB\xBF" // BOM
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            );
        {
            std::ofstream ofs("tmp.toml");
            ofs << table;
        }
        const auto data = toml::parse("tmp.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "\xEF\xBB\xBF" // BOM
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\"\r\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss, "test_file_with_BOM_CRLF.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "\xEF\xBB\xBF" // BOM
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\"\r\n"
            );
        {
            // with text-mode, "\n" is converted to "\r\n" and the resulting
            // value will be "\r\r\n". To avoid the additional "\r", use binary
            // mode.
            std::ofstream ofs("tmp.toml", std::ios_base::binary);
            ofs.write(table.data(), static_cast<std::streamsize>(table.size()));
        }
        const auto data = toml::parse("tmp.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
}

BOOST_AUTO_TEST_CASE(test_file_without_newline_at_the_end_of_file)
{
    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\""
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_file_without_newline_at_the_end_of_file.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\""
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_file_without_newline_at_the_end_of_file_CRLF.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }

    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\" # comment"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_file_without_newline_at_the_end_of_file_comment.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\" # comment"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_file_without_newline_at_the_end_of_file_comment.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }

    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\" \t"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_file_without_newline_at_the_end_of_file_ws.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\" \t"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_file_without_newline_at_the_end_of_file_ws.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
}


BOOST_AUTO_TEST_CASE(test_files_end_with_comment)
{
    // comment w/o newline
    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            "# comment"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_comment.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            "# comment\n"
            "# one more comment"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_comment.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }

    // comment w/ newline

    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            "# comment\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_comment.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            "# comment\n"
            "# one more comment\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_comment.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }

    // CRLF version

    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\"\r\n"
            "# comment"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_comment.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\"\r\n"
            "# comment\r\n"
            "# one more comment"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_comment.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\"\r\n"
            "# comment\r\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_comment.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\"\r\n"
            "# comment\r\n"
            "# one more comment\r\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_comment.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
}


BOOST_AUTO_TEST_CASE(test_files_end_with_empty_lines)
{
    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            "\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            "\n"
            "\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }

    // with whitespaces

    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            "  \n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            "  \n"
            "  \n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            "\n"
            "  \n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            "  \n"
            "\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }

    // with whitespaces but no newline
    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            "  "
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }

    // without newline
    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\"\n"
            "a = 0"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }


    // CRLF

    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\"\r\n"
            "\r\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\"\r\n"
            "\r\n"
            "\r\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }

    // with whitespaces

    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\"\r\n"
            "  \r\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\"\r\n"
            "\r\n"
            "  \r\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\"\r\n"
            "  \r\n"
            "\r\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\"\r\n"
            "  \r\n"
            "  \r\n"
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
    {
        const std::string table(
            "key = \"value\"\r\n"
            "[table]\r\n"
            "key = \"value\"\r\n"
            "  "
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_with_newline.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
}

BOOST_AUTO_TEST_CASE(test_file_ends_without_lf)
{
    {
        const std::string table(
            "key = \"value\"\n"
            "[table]\n"
            "key = \"value\""
            );
        std::istringstream iss(table);
        const auto data = toml::parse(iss,
                "test_files_end_without_lf.toml");

        BOOST_TEST(toml::find<std::string>(data, "key") == "value");
        BOOST_TEST(toml::find<std::string>(toml::find(data, "table"), "key") == "value");
    }
}

BOOST_AUTO_TEST_CASE(test_parse_function_compiles)
{
    using result_type = decltype(toml::parse("string literal"));
    (void) [](const char* that) -> result_type { return toml::parse(that); };
    (void) [](char* that) -> result_type { return toml::parse(that); };
    (void) [](const std::string& that) -> result_type { return toml::parse(that); };
    (void) [](std::string& that) -> result_type { return toml::parse(that); };
    (void) [](std::string&& that) -> result_type { return toml::parse(that); };
#ifdef TOML11_HAS_STD_FILESYSTEM
    (void) [](const std::filesystem::path& that) -> result_type { return toml::parse(that); };
    (void) [](std::filesystem::path& that) -> result_type { return toml::parse(that); };
    (void) [](std::filesystem::path&& that) -> result_type { return toml::parse(that); };
#endif
    (void) [](std::FILE* that) -> result_type { return toml::parse(that, "mandatory.toml"); };
}

BOOST_AUTO_TEST_CASE(test_parse_nonexistent_file)
{
    BOOST_CHECK_THROW(toml::parse("nonexistent.toml"), std::ios_base::failure);
}
