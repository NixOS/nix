#include "util.hh"
#include "get-drvs.hh"


namespace nix {


static void readLegacyManifest(const Path & path, DrvInfos & elems);


DrvInfos queryInstalled(EvalState & state, const Path & userEnv)
{
    DrvInfos elems;
    
    Path path = userEnv + "/manifest";

    if (!pathExists(path))
        return DrvInfos(); /* not an error, assume nothing installed */

    readLegacyManifest(path, elems);

    return elems;
}


/* Code for parsing manifests in the old textual ATerm format. */

static void expect(std::istream & str, const string & s)
{
    char s2[s.size()];
    str.read(s2, s.size());
    if (string(s2, s.size()) != s)
        throw Error(format("expected string `%1%'") % s);
}


static string parseString(std::istream & str)
{
    string res;
    expect(str, "\"");
    int c;
    while ((c = str.get()) != '"')
        if (c == '\\') {
            c = str.get();
            if (c == 'n') res += '\n';
            else if (c == 'r') res += '\r';
            else if (c == 't') res += '\t';
            else res += c;
        }
        else res += c;
    return res;
}


static string parseStr(std::istream & str)
{
    expect(str, "Str(");
    string s = parseString(str);
    expect(str, ",[])");
    return s;
}


static string parseWord(std::istream & str)
{
    string res;
    while (isalpha(str.peek()))
        res += str.get();
    return res;
}


static bool endOfList(std::istream & str)
{
    if (str.peek() == ',') {
        str.get();
        return false;
    }
    if (str.peek() == ']') {
        str.get();
        return true;
    }
    return false;
}


static MetaInfo parseMeta(std::istream & str)
{
    MetaInfo meta;

    expect(str, "Attrs([");
    while (!endOfList(str)) {
        expect(str, "Bind(");

        MetaValue value;
        
        string name = parseString(str);
        expect(str, ",");

        string type = parseWord(str);

        if (type == "Str") {
            expect(str, "(");
            value.type = MetaValue::tpString;
            value.stringValue = parseString(str);
            expect(str, ",[])");
        }

        else if (type == "List") {
            expect(str, "([");
            value.type = MetaValue::tpStrings;
            while (!endOfList(str))
                value.stringValues.push_back(parseStr(str));
            expect(str, ")");
        }

        else throw Error(format("unexpected token `%1%'") % type);

        expect(str, ",NoPos)");
        meta[name] = value;
    }
    
    expect(str, ")");

    return meta;
}


static void readLegacyManifest(const Path & path, DrvInfos & elems)
{
    string manifest = readFile(path);
    std::istringstream str(manifest);
    expect(str, "List([");

    unsigned int n = 0;
    
    while (!endOfList(str)) {
        DrvInfo elem;
        expect(str, "Attrs([");

        while (!endOfList(str)) {
            expect(str, "Bind(");
            string name = parseString(str);
            expect(str, ",");
            
            if (name == "meta") elem.setMetaInfo(parseMeta(str));
            else {
                string value = parseStr(str);
                if (name == "name") elem.name = value;
                else if (name == "outPath") elem.setOutPath(value);
                else if (name == "drvPath") elem.setDrvPath(value);
                else if (name == "system") elem.system = value;
            }

            expect(str, ",NoPos)");
        }

        expect(str, ")");

        if (elem.name != "") {
            elem.attrPath = int2String(n++);
            elems.push_back(elem);
        }
    }

    expect(str, ")");
}


}

