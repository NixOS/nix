#include "dotgraph.hh"


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


string pathLabel(const FSId & id, const string & path)
{
    return (string) id + "-" + path;
}


void printClosure(const FSId & id, const NixExpr & fs)
{
    Strings workList(fs.closure.roots.begin(), fs.closure.roots.end());
    StringSet doneSet;

    for (Strings::iterator i = workList.begin(); i != workList.end(); i++) {
	cout << makeEdge(pathLabel(id, *i), id);
    }

    while (!workList.empty()) {
	string path = workList.front();
	workList.pop_front();

	if (doneSet.find(path) == doneSet.end()) {
	    doneSet.insert(path);

	    ClosureElems::const_iterator elem = fs.closure.elems.find(path);
	    if (elem == fs.closure.elems.end())
		throw Error(format("bad closure, missing path `%1%'") % path);

	    for (StringSet::const_iterator i = elem->second.refs.begin();
		 i != elem->second.refs.end(); i++)
	    {
		workList.push_back(*i);
		cout << makeEdge(pathLabel(id, *i), pathLabel(id, path));
	    }

	    cout << makeNode(pathLabel(id, path), 
		symbolicName(path), "#ff0000");
	}
    }
}


void printDotGraph(const FSIds & roots)
{
    FSIds workList(roots.begin(), roots.end());
    FSIdSet doneSet;
            
    cout << "digraph G {\n";

    while (!workList.empty()) {
	FSId id = workList.front();
	workList.pop_front();

	if (doneSet.find(id) == doneSet.end()) {
	    doneSet.insert(id);
                    
	    NixExpr ne = parseNixExpr(termFromId(id));

	    string label, colour;
                    
	    if (ne.type == NixExpr::neDerivation) {
		for (FSIdSet::iterator i = ne.derivation.inputs.begin();
		     i != ne.derivation.inputs.end(); i++)
		{
		    workList.push_back(*i);
		    cout << makeEdge(*i, id);
		}

		label = "derivation";
		colour = "#00ff00";
		for (StringPairs::iterator i = ne.derivation.env.begin();
		     i != ne.derivation.env.end(); i++)
		    if (i->first == "name") label = i->second;
	    }

	    else if (ne.type == NixExpr::neClosure) {
		label = "<closure>";
		colour = "#00ffff";
		printClosure(id, ne);
	    }

	    else abort();

	    cout << makeNode(id, label, colour);
	}
    }

    cout << "}\n";
}
