#include "xmlgraph.hh"
#include "util.hh"
#include "store-api.hh"

#include <iostream>


using std::cout;

namespace nix {


static inline const string & xmlQuote(const string & s)
{
    // Luckily, store paths shouldn't contain any character that needs to be
    // quoted.
    return s;
}


static string makeEdge(const string & src, const string & dst)
{
    format f = format("  <edge src=\"%1%\" dst=\"%2%\"/>\n")
      % xmlQuote(src) % xmlQuote(dst);
    return f.str();
}


static string makeNode(const string & id)
{
    format f = format("  <node name=\"%1%\"/>\n") % xmlQuote(id);
    return f.str();
}


void printXmlGraph(const PathSet & roots)
{
    PathSet workList(roots);
    PathSet doneSet;

    cout << "<?xml version='1.0' encoding='utf-8'?>\n"
	 << "<nix>\n";

    while (!workList.empty()) {
	Path path = *(workList.begin());
	workList.erase(path);

	if (doneSet.find(path) != doneSet.end()) continue;
	doneSet.insert(path);

	cout << makeNode(path);

	PathSet references;
	store->queryReferences(path, references);

	for (PathSet::iterator i = references.begin();
	     i != references.end(); ++i)
	{
	    if (*i != path) {
		workList.insert(*i);
		cout << makeEdge(*i, path);
	    }
	}

    }

    cout << "</nix>\n";
}


}
