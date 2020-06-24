#include "dotgraph.hh"
#include "util.hh"
#include "store-api.hh"

#include <iostream>


using std::cout;

namespace nix {


static string dotQuote(std::string_view s)
{
    return "\"" + std::string(s) + "\"";
}


static string nextColour()
{
    static int n = 0;
    static string colours[] =
        { "black", "red", "green", "blue"
        , "magenta", "burlywood" };
    return colours[n++ % (sizeof(colours) / sizeof(string))];
}


static string makeEdge(const string & src, const string & dst)
{
    format f = format("%1% -> %2% [color = %3%];\n")
        % dotQuote(src) % dotQuote(dst) % dotQuote(nextColour());
    return f.str();
}


static string makeNode(const string & id, std::string_view label,
    const string & colour)
{
    format f = format("%1% [label = %2%, shape = box, "
        "style = filled, fillcolor = %3%];\n")
        % dotQuote(id) % dotQuote(label) % dotQuote(colour);
    return f.str();
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
