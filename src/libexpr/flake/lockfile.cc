#include "lockfile.hh"
#include "store-api.hh"
#include "url-parts.hh"

#include <nlohmann/json.hpp>

namespace nix::flake {

FlakeRef getFlakeRef(
    const nlohmann::json & json,
    const char * attr,
    const char * info)
{
    auto i = json.find(attr);
    if (i != json.end()) {
        auto attrs = jsonToAttrs(*i);
        // FIXME: remove when we drop support for version 5.
        if (info) {
            auto j = json.find(info);
            if (j != json.end()) {
                for (auto k : jsonToAttrs(*j))
                    attrs.insert_or_assign(k.first, k.second);
            }
        }
        return FlakeRef::fromAttrs(attrs);
    }

    throw Error("attribute '%s' missing in lock file", attr);
}

LockedNode::LockedNode(const nlohmann::json & json)
    : lockedRef(getFlakeRef(json, "locked", "info"))
    , originalRef(getFlakeRef(json, "original", nullptr))
    , isFlake(json.find("flake") != json.end() ? (bool) json["flake"] : true)
{
    if (!lockedRef.input.isImmutable())
        throw Error("lockfile contains mutable lock '%s'", attrsToJson(lockedRef.input.toAttrs()));
}

StorePathDescriptor LockedNode::computeStorePath(Store & store) const
{
    return lockedRef.input.computeStorePath(store);
}

std::shared_ptr<Node> LockFile::findInput(const InputPath & path)
{
    auto pos = root;

    if (!pos) return {};

    for (auto & elem : path) {
        if (auto i = get(pos->inputs, elem)) {
            if (auto node = std::get_if<0>(&*i))
                pos = *node;
            else if (auto follows = std::get_if<1>(&*i)) {
                pos = findInput(*follows);
                if (!pos) return {};
            }
        } else
            return {};
    }

    return pos;
}

LockFile::LockFile(const nlohmann::json & json, const Path & path)
{
    auto version = json.value("version", 0);
    if (version < 5 || version > 7)
        throw Error("lock file '%s' has unsupported version %d", path, version);

    std::unordered_map<std::string, std::shared_ptr<Node>> nodeMap;

    std::function<void(Node & node, const nlohmann::json & jsonNode)> getInputs;

    getInputs = [&](Node & node, const nlohmann::json & jsonNode)
    {
        if (jsonNode.find("inputs") == jsonNode.end()) return;
        for (auto & i : jsonNode["inputs"].items()) {
            if (i.value().is_array()) {
                InputPath path;
                for (auto & j : i.value())
                    path.push_back(j);
                node.inputs.insert_or_assign(i.key(), path);
            } else {
                std::string inputKey = i.value();
                auto k = nodeMap.find(inputKey);
                if (k == nodeMap.end()) {
                    auto jsonNode2 = json["nodes"][inputKey];
                    auto input = std::make_shared<LockedNode>(jsonNode2);
                    k = nodeMap.insert_or_assign(inputKey, input).first;
                    getInputs(*input, jsonNode2);
                }
                if (auto child = std::dynamic_pointer_cast<LockedNode>(k->second))
                    node.inputs.insert_or_assign(i.key(), child);
                else
                    // FIXME: replace by follows node
                    throw Error("lock file contains cycle to root node");
            }
        }
    };

    std::string rootKey = json["root"];
    nodeMap.insert_or_assign(rootKey, root);
    getInputs(*root, json["nodes"][rootKey]);

    // FIXME: check that there are no cycles in version >= 7. Cycles
    // between inputs are only possible using 'follows' indirections.
    // Once we drop support for version <= 6, we can simplify the code
    // a bit since we don't need to worry about cycles.
}

nlohmann::json LockFile::toJson() const
{
    nlohmann::json nodes;
    std::unordered_map<std::shared_ptr<const Node>, std::string> nodeKeys;
    std::unordered_set<std::string> keys;

    std::function<std::string(const std::string & key, std::shared_ptr<const Node> node)> dumpNode;

    dumpNode = [&](std::string key, std::shared_ptr<const Node> node) -> std::string
    {
        auto k = nodeKeys.find(node);
        if (k != nodeKeys.end())
            return k->second;

        if (!keys.insert(key).second) {
            for (int n = 2; ; ++n) {
                auto k = fmt("%s_%d", key, n);
                if (keys.insert(k).second) {
                    key = k;
                    break;
                }
            }
        }

        nodeKeys.insert_or_assign(node, key);

        auto n = nlohmann::json::object();

        if (!node->inputs.empty()) {
            auto inputs = nlohmann::json::object();
            for (auto & i : node->inputs) {
                if (auto child = std::get_if<0>(&i.second)) {
                    inputs[i.first] = dumpNode(i.first, *child);
                } else if (auto follows = std::get_if<1>(&i.second)) {
                    auto arr = nlohmann::json::array();
                    for (auto & x : *follows)
                        arr.push_back(x);
                    inputs[i.first] = std::move(arr);
                }
            }
            n["inputs"] = std::move(inputs);
        }

        if (auto lockedNode = std::dynamic_pointer_cast<const LockedNode>(node)) {
            n["original"] = fetchers::attrsToJson(lockedNode->originalRef.toAttrs());
            n["locked"] = fetchers::attrsToJson(lockedNode->lockedRef.toAttrs());
            if (!lockedNode->isFlake) n["flake"] = false;
        }

        nodes[key] = std::move(n);

        return key;
    };

    nlohmann::json json;
    json["version"] = 7;
    json["root"] = dumpNode("root", root);
    json["nodes"] = std::move(nodes);

    return json;
}

std::string LockFile::to_string() const
{
    return toJson().dump(2);
}

LockFile LockFile::read(const Path & path)
{
    if (!pathExists(path)) return LockFile();
    return LockFile(nlohmann::json::parse(readFile(path)), path);
}

std::ostream & operator <<(std::ostream & stream, const LockFile & lockFile)
{
    stream << lockFile.toJson().dump(2);
    return stream;
}

void LockFile::write(const Path & path) const
{
    createDirs(dirOf(path));
    writeFile(path, fmt("%s\n", *this));
}

bool LockFile::isImmutable() const
{
    std::unordered_set<std::shared_ptr<const Node>> nodes;

    std::function<void(std::shared_ptr<const Node> node)> visit;

    visit = [&](std::shared_ptr<const Node> node)
    {
        if (!nodes.insert(node).second) return;
        for (auto & i : node->inputs)
            if (auto child = std::get_if<0>(&i.second))
                visit(*child);
    };

    visit(root);

    for (auto & i : nodes) {
        if (i == root) continue;
        auto lockedNode = std::dynamic_pointer_cast<const LockedNode>(i);
        if (lockedNode && !lockedNode->lockedRef.input.isImmutable()) return false;
    }

    return true;
}

bool LockFile::operator ==(const LockFile & other) const
{
    // FIXME: slow
    return toJson() == other.toJson();
}

InputPath parseInputPath(std::string_view s)
{
    InputPath path;

    for (auto & elem : tokenizeString<std::vector<std::string>>(s, "/")) {
        if (!std::regex_match(elem, flakeIdRegex))
            throw UsageError("invalid flake input path element '%s'", elem);
        path.push_back(elem);
    }

    return path;
}

std::map<InputPath, Node::Edge> LockFile::getAllInputs() const
{
    std::unordered_set<std::shared_ptr<Node>> done;
    std::map<InputPath, Node::Edge> res;

    std::function<void(const InputPath & prefix, std::shared_ptr<Node> node)> recurse;

    recurse = [&](const InputPath & prefix, std::shared_ptr<Node> node)
    {
        if (!done.insert(node).second) return;

        for (auto &[id, input] : node->inputs) {
            auto inputPath(prefix);
            inputPath.push_back(id);
            res.emplace(inputPath, input);
            if (auto child = std::get_if<0>(&input))
                recurse(inputPath, *child);
        }
    };

    recurse({}, root);

    return res;
}

std::ostream & operator <<(std::ostream & stream, const Node::Edge & edge)
{
    if (auto node = std::get_if<0>(&edge))
        stream << "'" << (*node)->lockedRef << "'";
    else if (auto follows = std::get_if<1>(&edge))
        stream << fmt("follows '%s'", printInputPath(*follows));
    return stream;
}

static bool equals(const Node::Edge & e1, const Node::Edge & e2)
{
    if (auto n1 = std::get_if<0>(&e1))
        if (auto n2 = std::get_if<0>(&e2))
            return (*n1)->lockedRef == (*n2)->lockedRef;
    if (auto f1 = std::get_if<1>(&e1))
        if (auto f2 = std::get_if<1>(&e2))
            return *f1 == *f2;
    return false;
}

std::string LockFile::diff(const LockFile & oldLocks, const LockFile & newLocks)
{
    auto oldFlat = oldLocks.getAllInputs();
    auto newFlat = newLocks.getAllInputs();

    auto i = oldFlat.begin();
    auto j = newFlat.begin();
    std::string res;

    while (i != oldFlat.end() || j != newFlat.end()) {
        if (j != newFlat.end() && (i == oldFlat.end() || i->first > j->first)) {
            res += fmt("* Added '%s': %s\n", printInputPath(j->first), j->second);
            ++j;
        } else if (i != oldFlat.end() && (j == newFlat.end() || i->first < j->first)) {
            res += fmt("* Removed '%s'\n", printInputPath(i->first));
            ++i;
        } else {
            if (!equals(i->second, j->second)) {
                res += fmt("* Updated '%s': %s -> %s\n",
                    printInputPath(i->first),
                    i->second,
                    j->second);
            }
            ++i;
            ++j;
        }
    }

    return res;
}

void LockFile::check()
{
    auto inputs = getAllInputs();

    for (auto & [inputPath, input] : inputs) {
        if (auto follows = std::get_if<1>(&input)) {
            if (!follows->empty() && !get(inputs, *follows))
                throw Error("input '%s' follows a non-existent input '%s'",
                    printInputPath(inputPath),
                    printInputPath(*follows));
        }
    }
}

void check();

std::string printInputPath(const InputPath & path)
{
    return concatStringsSep("/", path);
}

}
