#include "dotgraph.hh"


static string dotQuote(const string & s)
{
    return "\"" + s + "\"";
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

	    string label, shape;
                    
	    if (fs.type == FState::fsDerive) {
		for (FSIdSet::iterator i = fs.derive.inputs.begin();
		     i != fs.derive.inputs.end(); i++)
		{
		    workList.push_back(*i);
		    cout << dotQuote(*i) << " -> "
			 << dotQuote(id) << ";\n";
		}

		label = "derive";
		shape = "box";
		for (StringPairs::iterator i = fs.derive.env.begin();
		     i != fs.derive.env.end(); i++)
		    if (i->first == "name") label = i->second;
	    }

	    else if (fs.type == FState::fsSlice) {
		label = baseNameOf((*fs.slice.elems.begin()).first);
		shape = "ellipse";
		if (isHash(string(label, 0, Hash::hashSize * 2)) && 
		    label[Hash::hashSize * 2] == '-')
		    label = string(label, Hash::hashSize * 2 + 1);
	    }

	    else abort();

	    cout << dotQuote(id) << "[label = "
		 << dotQuote(label)
		 << ", shape = " << shape
		 << "];\n";
	}
    }

    cout << "}\n";

}
