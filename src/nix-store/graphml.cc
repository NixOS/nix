#include "graphml.hh"
#include "util.hh"
#include "store-api.hh"
#include "derivations.hh"

#include <iostream>


using std::cout;

namespace nix {


static inline std::string_view xmlQuote(std::string_view s)
{
    // Luckily, store paths shouldn't contain any character that needs to be
    // quoted.
    return s;
}


static std::string symbolicName(std::string_view p)
{
    return std::string(p.substr(0, p.find('-') + 1));
}


static std::string makeEdge(std::string_view src, std::string_view dst)
{
    return fmt("  <edge source=\"%1%\" target=\"%2%\"/>\n",
        xmlQuote(src), xmlQuote(dst));
}


static std::string makeNode(const ValidPathInfo & info)
{
    return fmt(
        "  <node id=\"%1%\">\n"
        "    <data key=\"narSize\">%2%</data>\n"
        "    <data key=\"name\">%3%</data>\n"
        "    <data key=\"type\">%4%</data>\n"
        "  </node>\n",
        info.path.to_string(),
        info.narSize,
        symbolicName(std::string(info.path.name())),
        (info.path.isDerivation() ? "derivation" : "output-path"));
}


void printGraphML(ref<Store> store, StorePathSet && roots)
{
    StorePathSet workList(std::move(roots));
    StorePathSet doneSet;
    std::pair<StorePathSet::iterator, bool> ret;

    cout << "<?xml version='1.0' encoding='utf-8'?>\n"
         << "<graphml xmlns='http://graphml.graphdrawing.org/xmlns'\n"
         << "    xmlns:xsi='http://www.w3.org/2001/XMLSchema-instance'\n"
         << "    xsi:schemaLocation='http://graphml.graphdrawing.org/xmlns/1.0/graphml.xsd'>\n"
         << "<key id='narSize' for='node' attr.name='narSize' attr.type='int'/>"
         << "<key id='name' for='node' attr.name='name' attr.type='string'/>"
         << "<key id='type' for='node' attr.name='type' attr.type='string'/>"
         << "<graph id='G' edgedefault='directed'>\n";

    while (!workList.empty()) {
        auto path = std::move(workList.extract(workList.begin()).value());

        ret = doneSet.insert(path);
        if (ret.second == false) continue;

        auto info = store->queryPathInfo(path);
        cout << makeNode(*info);

        for (auto & p : info->referencesPossiblyToSelf()) {
            if (p != path) {
                workList.insert(p);
                cout << makeEdge(path.to_string(), p.to_string());
            }
        }

    }

    cout << "</graph>\n";
    cout << "</graphml>\n";
}


}
