#include "dotgraph.hh"
#include "normalise.hh"


static string dotQuote(const string & s)
{
    return "\"" + s + "\"";
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


static string makeNode(const string & id, const string & label,
    const string & colour)
{
    format f = format("%1% [label = %2%, shape = box, "
	"style = filled, fillcolor = %3%];\n")
	% dotQuote(id) % dotQuote(label) % dotQuote(colour);
    return f.str();
}


static string symbolicName(const string & path)
{
    string p = baseNameOf(path);
    if (isHash(string(p, 0, Hash::hashSize * 2)) && 
	p[Hash::hashSize * 2] == '-')
	p = string(p, Hash::hashSize * 2 + 1);
    return p;
}


string pathLabel(const Path & nePath, const string & elemPath)
{
    return (string) nePath + "-" + elemPath;
}


void printClosure(const Path & nePath, const StoreExpr & fs)
{
    PathSet workList(fs.closure.roots);
    PathSet doneSet;

    for (PathSet::iterator i = workList.begin(); i != workList.end(); i++) {
	cout << makeEdge(pathLabel(nePath, *i), nePath);
    }

    while (!workList.empty()) {
	Path path = *(workList.begin());
	workList.erase(path);

	if (doneSet.find(path) == doneSet.end()) {
	    doneSet.insert(path);

	    ClosureElems::const_iterator elem = fs.closure.elems.find(path);
	    if (elem == fs.closure.elems.end())
		throw Error(format("bad closure, missing path `%1%'") % path);

	    for (StringSet::const_iterator i = elem->second.refs.begin();
		 i != elem->second.refs.end(); i++)
	    {
		workList.insert(*i);
		cout << makeEdge(pathLabel(nePath, *i), pathLabel(nePath, path));
	    }

	    cout << makeNode(pathLabel(nePath, path), 
		symbolicName(path), "#ff0000");
	}
    }
}


void printDotGraph(const PathSet & roots)
{
    PathSet workList(roots);
    PathSet doneSet;
            
    cout << "digraph G {\n";

    while (!workList.empty()) {
	Path nePath = *(workList.begin());
	workList.erase(nePath);

	if (doneSet.find(nePath) == doneSet.end()) {
	    doneSet.insert(nePath);

	    StoreExpr ne = storeExprFromPath(nePath);

	    string label, colour;
                    
	    if (ne.type == StoreExpr::neDerivation) {
		for (PathSet::iterator i = ne.derivation.inputs.begin();
		     i != ne.derivation.inputs.end(); i++)
		{
		    workList.insert(*i);
		    cout << makeEdge(*i, nePath);
		}

		label = "derivation";
		colour = "#00ff00";
		for (StringPairs::iterator i = ne.derivation.env.begin();
		     i != ne.derivation.env.end(); i++)
		    if (i->first == "name") label = i->second;
	    }

	    else if (ne.type == StoreExpr::neClosure) {
		label = "<closure>";
		colour = "#00ffff";
		printClosure(nePath, ne);
	    }

	    else abort();

	    cout << makeNode(nePath, label, colour);
	}
    }

    cout << "}\n";
}
