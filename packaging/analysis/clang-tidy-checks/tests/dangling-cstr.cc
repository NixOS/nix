// Test corpus for nix-dangling-cstr check.
// RUN: clang-tidy --checks='-*,nix-dangling-cstr' %s --

#include <string>

std::string getPath();
std::string makeName(int id);

// --- Positive cases: dangling pointer from temporary ---

void bad_function_return()
{
    const char * p = getPath().c_str(); // warn: temporary destroyed
    (void) p;
}

void bad_concatenation()
{
    std::string a = "hello";
    const char * p = (a + "/world").c_str(); // warn: temporary destroyed
    (void) p;
}

void bad_data_call()
{
    const char * p = getPath().data(); // warn: .data() on temporary
    (void) p;
}

void bad_substr()
{
    std::string s = "hello world";
    const char * p = s.substr(0, 5).c_str(); // warn: temporary from substr
    (void) p;
}

// --- Negative cases: pointer from named variable ---

void good_named_variable()
{
    std::string s = getPath();
    const char * p = s.c_str(); // ok: s lives beyond this line
    (void) p;
}

void good_direct_use()
{
    // Using c_str() as a function argument is fine — the temporary
    // lives until the end of the full-expression.
    printf("%s\n", getPath().c_str()); // ok
}

void good_member_access()
{
    struct Foo
    {
        std::string name;
    };

    Foo f;
    f.name = "test";
    const char * p = f.name.c_str(); // ok: f.name is not a temporary
    (void) p;
}

void good_local_string()
{
    std::string path = "/nix/store";
    const char * p = path.data(); // ok: named variable
    (void) p;
}
