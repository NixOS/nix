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


void printSlice(const FSId & id, const FState & fs)
{
    Strings workList(fs.slice.roots.begin(), fs.slice.roots.end());
    StringSet doneSet;

    for (Strings::iterator i = workList.begin(); i != workList.end(); i++) {
	cout << makeEdge(pathLabel(id, *i), id);
    }

    while (!workList.empty()) {
	string path = workList.front();
	workList.pop_front();

	if (doneSet.find(path) == doneSet.end()) {
	    doneSet.insert(path);

	    SliceElems::const_iterator elem = fs.slice.elems.find(path);
	    if (elem == fs.slice.elems.end())
		throw Error(format("bad slice, missing path `%1%'") % path);

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
                    
	    FState fs = parseFState(termFromId(id));

	    string label, colour;
                    
	    if (fs.type == FState::fsDerive) {
		for (FSIdSet::iterator i = fs.derive.inputs.begin();
		     i != fs.derive.inputs.end(); i++)
		{
		    workList.push_back(*i);
		    cout << makeEdge(*i, id);
		}

		label = "derive";
		colour = "#00ff00";
		for (StringPairs::iterator i = fs.derive.env.begin();
		     i != fs.derive.env.end(); i++)
		    if (i->first == "name") label = i->second;
	    }

	    else if (fs.type == FState::fsSlice) {
		label = "<slice>";
		colour = "#00ffff";
		printSlice(id, fs);
	    }

	    else abort();

	    cout << makeNode(id, label, colour);
	}
    }

    cout << "}\n";
}
