#include <iostream>
#include <cstdlib>
#include <queue>

#include "shared.hh"
#include "eval.hh"

using namespace nix;

string pathToAttrName(const Path & path)
{
    string res;
    for (auto & c : path)
        if (c == '/') res += '_';
        else if (c == '.') res += '_';
        else if (c == '+') res += '_';
        else res += c;
    return "_file_" + res;
}

void packFile(EvalState & state, Path root, const string & startFile)
{
    root = canonPath(root);

    std::queue<string> queue({startFile});
    std::set<string> done;

    std::cout << "let\n\n";

    while (!queue.empty()) {
        string file = queue.front();
        queue.pop();
        if (done.find(file) != done.end()) continue;
        done.insert(file);
        Path path = resolveExprPath(root + "/" + file);
        std::cerr << "processing " << path << "\n";

        Expr & ast(*state.parseExprFromFile(path));

        Expr::Visitor visitor = [&](Expr & e) -> Expr * {
            ExprApp * app = dynamic_cast<ExprApp *>(&e);
            if (app) {
                if (!app) return &e;
                ExprVar * var = dynamic_cast<ExprVar *>(app->e1);
                if (!var) return &e;
                string fnName = var->name;
                if (fnName != "import" &&
                    fnName != "callPackage" &&
                    fnName != "callPackage_i686" &&
                    fnName != "builderDefsPackage") return &e;
                ExprPath * path = dynamic_cast<ExprPath *>(app->e2);
                if (!path) return &e;
                string file2 = path->s;
                if (file2.empty() || file2[0] == '/') return &e;
                //std::cerr << " found " << file2 << "\n";
                queue.push(file2);
                Expr * res = new ExprVar(state.symbols.create(pathToAttrName(file2)));
                if ((string) var->name == "import") return res;
                app->e2 = res;
                return app;
            }

            ExprPath * path = dynamic_cast<ExprPath *>(&e);
            if (path) {
                string old = path->s;
                if (path->s == root) path->s = "./.";
                else if (string(path->s, 0, root.size()) == root && string(path->s, root.size(), 1) == "/") {
                    string file2(path->s, root.size() + 1);
                    path->s = file2.find('/') == string::npos ? "./" + file2 : file2;
                }
            }

            return &e;
        };

        Expr * astNew = ast.rewrite(visitor);

        std::cout << "# " << file << "\n";
        std::cout << state.symbols.create(pathToAttrName(file)) << " = " << *astNew << ";\n\n";
    }

    std::cout << "in " << pathToAttrName(startFile) << "\n";
}

int main(int argc, char * * argv)
{
    return handleExceptions(argv[0], [&]() {
        initNix();
        EvalState state = EvalState(Strings());
        packFile(state, "/home/eelco/Dev/nixpkgs-stable", "default.nix");
    });
}
