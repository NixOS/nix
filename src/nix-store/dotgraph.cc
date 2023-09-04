#include "dotgraph.hh"
#include "util.hh"
#include "store-api.hh"

#include <iostream>


using std::cout;

namespace nix {


static std::string dotQuote(std::string_view s)
{
    return "\"" + std::string(s) + "\"";
}


static const std::string & nextColour()
{
    static int n = 0;
    static std::vector<std::string> colours
        { "black", "red", "green", "blue"
        , "magenta", "burlywood" };
    return colours[n++ % colours.size()];
}


static std::string makeEdge(std::string_view src, std::string_view dst)
{
    return fmt("%1% -> %2% [color = %3%];\n",
        dotQuote(src), dotQuote(dst), dotQuote(nextColour()));
}


static std::string makeNode(std::string_view id, std::string_view label,
    std::string_view colour)
{
    return fmt("%1% [label = %2%, shape = box, "
        "style = filled, fillcolor = %3%];\n",
        dotQuote(id), dotQuote(label), dotQuote(colour));
}


void printDotGraph(ref<Store> store, StorePathSet && roots)
{
    StorePathSet workList(std::move(roots));
    StorePathSet doneSet;

    cout << "digraph G {\n";

    while (!workList.empty()) {
        auto path = std::move(workList.extract(workList.begin()).value());

        if (!doneSet.insert(path).second) continue;

        cout << makeNode(std::string(path.to_string()), path.name(), "#ff0000");

        for (auto & p : store->queryPathInfo(path)->referencesPossiblyToSelf()) {
            if (p != path) {
                workList.insert(p);
                cout << makeEdge(std::string(p.to_string()), std::string(path.to_string()));
            }
        }
    }

    cout << "}\n";
}


}
