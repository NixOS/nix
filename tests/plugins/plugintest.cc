#include "config.hh"
#include "primops.hh"
#include "repl.hh"

#include <iostream>

using namespace nix;

struct MySettings : Config
{
    Setting<bool> settingSet{this, false, "setting-set",
        "Whether the plugin-defined setting was set"};
};

MySettings mySettings;

static GlobalConfig::Register rs(&mySettings);

static void prim_anotherNull (EvalState & state, const Pos & pos, Value ** args, Value & v)
{
    if (mySettings.settingSet)
        mkNull(v);
    else
        mkBool(v, false);
}

static RegisterPrimOp rp("anotherNull", 0, prim_anotherNull);

static void myGreet (string name, string arg)
{
    std::cout << arg << " " << name << "!\n";
}

static RegisterReplCmd rc(vector<string>{"greet", "mySecondGreet"}, "aaaaa", myGreet, "ph");
