#include <toml.hpp>

#include <iomanip>
#include <iostream>

struct json_serializer
{
    void operator()(toml::boolean v)
    {
        std::cout << "{\"type\":\"bool\",\"value\":\"" << toml::value(v) << "\"}";
        return ;
    }
    void operator()(toml::integer v)
    {
        std::cout << "{\"type\":\"integer\",\"value\":\"" << toml::value(v) << "\"}";
        return ;
    }
    void operator()(toml::floating v)
    {
        if(std::isnan(v) && std::signbit(v))
        {
            // toml-test does not allow negative NaN represented in "-nan" because
            // there are languages that does not distinguish nan and -nan.
            // But toml11 keeps sign from input. To resolve this difference,
            // we convert -nan to nan here.
            v = std::numeric_limits<toml::floating>::quiet_NaN();
        }
        std::cout << "{\"type\":\"float\",\"value\":\"" << toml::value(v) << "\"}";
        return ;
    }
    void operator()(const toml::string& v)
    {
        // since toml11 automatically convert string to multiline string that is
        // valid only in TOML, we need to format the string to make it valid in
        // JSON.
        toml::serializer<toml::value> ser(std::numeric_limits<std::size_t>::max());
        std::cout << "{\"type\":\"string\",\"value\":"
                  << ser(v.str) << "}";
        return ;
    }
    void operator()(const toml::local_time& v)
    {
        std::cout << "{\"type\":\"time-local\",\"value\":\"" << toml::value(v) << "\"}";
        return ;
    }
    void operator()(const toml::local_date& v)
    {
        std::cout << "{\"type\":\"date-local\",\"value\":\"" << toml::value(v) << "\"}";
        return ;
    }
    void operator()(const toml::local_datetime& v)
    {
        std::cout << "{\"type\":\"datetime-local\",\"value\":\"" << toml::value(v) << "\"}";
        return ;
    }
    void operator()(const toml::offset_datetime& v)
    {
        std::cout << "{\"type\":\"datetime\",\"value\":\"" << toml::value(v) << "\"}";
        return ;
    }
    void operator()(const toml::array& v)
    {
        if(!v.empty() && v.front().is_table())
        {
            std::cout << '[';
            bool is_first = true;
            for(const auto& elem : v)
            {
                if(!is_first) {std::cout << ", ";}
                is_first = false;
                toml::visit(*this, elem);
            }
            std::cout << ']';
        }
        else
        {
//             std::cout << "{\"type\":\"array\",\"value\":[";
            std::cout << "[";
            bool is_first = true;
            for(const auto& elem : v)
            {
                if(!is_first) {std::cout << ", ";}
                is_first = false;
                toml::visit(*this, elem);
            }
            std::cout << "]";
        }
        return ;
    }
    void operator()(const toml::table& v)
    {
        std::cout << '{';
        bool is_first = true;
        for(const auto& elem : v)
        {
            if(!is_first) {std::cout << ", ";}
            is_first = false;
            const auto k = toml::format_key(elem.first);
            if(k.at(0) == '"')
            {
                std::cout << k << ":";
            }
            else // bare key
            {
                std::cout << '\"' << k << "\":";
            }
            toml::visit(*this, elem.second);
        }
        std::cout << '}';
        return ;
    }
};

int main()
{
    try
    {
        std::vector<char> buf;
        std::cin.peek();
        while(!std::cin.eof())
        {
            buf.push_back(std::cin.get());
            std::cin.peek();
        }
        std::string bufstr(buf.begin(), buf.end());

        std::istringstream ss(bufstr);

        const auto data = toml::parse(ss);
        std::cout << std::setprecision(std::numeric_limits<double>::max_digits10);
        toml::visit(json_serializer(), data);
        return 0;
    }
    catch(const toml::syntax_error& err)
    {
        std::cout << "what(): " << err.what() << std::endl;
        return 1;
    }
}
