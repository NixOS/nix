#include "util.hh"
#include "versions.hh"

namespace nix {


string nextComponent(string::const_iterator & p,
    const string::const_iterator end)
{
    /* Skip any dots and dashes (component separators). */
    while (p != end && (*p == '.' || *p == '-')) ++p;

    if (p == end) return "";

    /* If the first character is a digit, consume the longest sequence
       of digits.  Otherwise, consume the longest sequence of
       non-digit, non-separator characters. */
    string s;
    if (isdigit(*p))
        while (p != end && isdigit(*p)) s += *p++;
    else
        while (p != end && (!isdigit(*p) && *p != '.' && *p != '-'))
            s += *p++;

    return s;
}


static bool componentsLT(const string & c1, const string & c2)
{
    int n1, n2;
    bool c1Num = string2Int(c1, n1), c2Num = string2Int(c2, n2);

    if (c1Num && c2Num) return n1 < n2;
    else if (c1 == "" && c2Num) return true;
    else if (c1 == "pre" && c2 != "pre") return true;
    else if (c2 == "pre") return false;
    /* Assume that `2.3a' < `2.3.1'. */
    else if (c2Num) return true;
    else if (c1Num) return false;
    else return c1 < c2;
}


int compareVersions(const string & v1, const string & v2)
{
    string::const_iterator p1 = v1.begin();
    string::const_iterator p2 = v2.begin();

    while (p1 != v1.end() || p2 != v2.end()) {
        string c1 = nextComponent(p1, v1.end());
        string c2 = nextComponent(p2, v2.end());
        if (componentsLT(c1, c2)) return -1;
        else if (componentsLT(c2, c1)) return 1;
    }

    return 0;
}


}
