#pragma once

namespace nlohmann {

struct json : basic_json<>
{
    using basic_json<>::basic_json;
};

}
