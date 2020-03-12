#include "lockfile.hh"
#include "store-api.hh"
#include "fetchers/regex.hh"

#include <nlohmann/json.hpp>

namespace nix::flake {

FlakeRef flakeRefFromJson(const nlohmann::json & json)
{
    return FlakeRef::fromAttrs(jsonToAttrs(json));
}

FlakeRef getFlakeRef(
    const nlohmann::json & json,
    const char * version3Attr1,
    const char * version3Attr2,
    const char * version4Attr)
{
    auto i = json.find(version4Attr);
    if (i != json.end())
        return flakeRefFromJson(*i);

    // FIXME: remove these.
    i = json.find(version3Attr1);
    if (i != json.end())
        return parseFlakeRef(*i);

    i = json.find(version3Attr2);
    if (i != json.end())
        return parseFlakeRef(*i);

    throw Error("attribute '%s' missing in lock file", version4Attr);
}

static TreeInfo parseTreeInfo(const nlohmann::json & json)
{
    TreeInfo info;

    auto i = json.find("info");
    if (i != json.end()) {
        const nlohmann::json & i2(*i);

        auto j = i2.find("narHash");
        if (j != i2.end())
            info.narHash = Hash((std::string) *j);
        else
            throw Error("attribute 'narHash' missing in lock file");

        j = i2.find("revCount");
        if (j != i2.end())
            info.revCount = *j;

        j = i2.find("lastModified");
        if (j != i2.end())
            info.lastModified = *j;

        return info;
    }

    i = json.find("narHash");
    if (i != json.end()) {
        info.narHash = Hash((std::string) *i);
        return info;
    }

    throw Error("attribute 'info' missing in lock file");
}

LockedNode::LockedNode(const nlohmann::json & json)
    : lockedRef(getFlakeRef(json, "url", "uri", "locked"))
    , originalRef(getFlakeRef(json, "originalUrl", "originalUri", "original"))
    , info(parseTreeInfo(json))
    , isFlake(json.find("flake") != json.end() ? (bool) json["flake"] : true)
{
    if (!lockedRef.input->isImmutable())
        throw Error("lockfile contains mutable flakeref '%s'", lockedRef);
}

static nlohmann::json treeInfoToJson(const TreeInfo & info)
{
    nlohmann::json json;
    assert(info.narHash);
    json["narHash"] = info.narHash.to_string(SRI);
    if (info.revCount)
        json["revCount"] = *info.revCount;
    if (info.lastModified)
        json["lastModified"] = *info.lastModified;
    return json;
}

StorePath LockedNode::computeStorePath(Store & store) const
{
    return info.computeStorePath(store);
}

std::shared_ptr<Node> Node::findInput(const InputPath & path)
{
    assert(!path.empty());

    auto pos = shared_from_this();

    for (auto & elem : path) {
        auto i = pos->inputs.find(elem);
        if (i == pos->inputs.end())
            return {};
        pos = i->second;
    }

    return pos;
}

LockFile::LockFile(const nlohmann::json & json, const Path & path)
{
    auto version = json.value("version", 0);
    if (version < 3 || version > 5)
        throw Error("lock file '%s' has unsupported version %d", path, version);

    if (version < 5) {
        std::function<void(Node & node, const nlohmann::json & json)> getInputs;

        getInputs = [&](Node & node, const nlohmann::json & json)
        {
            for (auto & i : json["inputs"].items()) {
                auto input = std::make_shared<LockedNode>(i.value());
                getInputs(*input, i.value());
                node.inputs.insert_or_assign(i.key(), input);
            }
        };

        getInputs(*root, json);
    }

    else {
        std::unordered_map<std::string, std::shared_ptr<Node>> nodeMap;

        std::function<void(Node & node, const nlohmann::json & jsonNode)> getInputs;

        getInputs = [&](Node & node, const nlohmann::json & jsonNode)
        {
            if (jsonNode.find("inputs") == jsonNode.end()) return;
            for (auto & i : jsonNode["inputs"].items()) {
                std::string inputKey = i.value();
                auto k = nodeMap.find(inputKey);
                if (k == nodeMap.end()) {
                    auto jsonNode2 = json["nodes"][inputKey];
                    auto input = std::make_shared<LockedNode>(jsonNode2);
                    k = nodeMap.insert_or_assign(inputKey, input).first;
                    getInputs(*input, jsonNode2);
                }
                node.inputs.insert_or_assign(i.key(), k->second);
            }
        };

        std::string rootKey = json["root"];
        nodeMap.insert_or_assign(rootKey, root);
        getInputs(*root, json["nodes"][rootKey]);
    }
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
            for (auto & i : node->inputs)
                inputs[i.first] = dumpNode(i.first, i.second);
            n["inputs"] = std::move(inputs);
        }

        if (auto lockedNode = std::dynamic_pointer_cast<const LockedNode>(node)) {
            n["original"] = fetchers::attrsToJson(lockedNode->originalRef.toAttrs());
            n["locked"] = fetchers::attrsToJson(lockedNode->lockedRef.toAttrs());
            n["info"] = treeInfoToJson(lockedNode->info);
            if (!lockedNode->isFlake) n["flake"] = false;
        }

        nodes[key] = std::move(n);

        return key;
    };

    nlohmann::json json;
    json["version"] = 5;
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
        for (auto & i : node->inputs) visit(i.second);
    };

    visit(root);

    for (auto & i : nodes) {
        if (i == root) continue;
        auto lockedNode = std::dynamic_pointer_cast<const LockedNode>(i);
        if (lockedNode && !lockedNode->lockedRef.input->isImmutable()) return false;
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
        if (!std::regex_match(elem, fetchers::flakeIdRegex))
            throw Error("invalid flake input path element '%s'", elem);
        path.push_back(elem);
    }

    if (path.empty())
        throw Error("flake input path is empty");

    return path;
}

static void flattenLockFile(
    std::shared_ptr<const Node> node,
    const InputPath & prefix,
    std::map<InputPath, std::shared_ptr<const LockedNode>> & res)
{
    // FIXME: handle cycles
    for (auto &[id, input] : node->inputs) {
        auto inputPath(prefix);
        inputPath.push_back(id);
        if (auto lockedInput = std::dynamic_pointer_cast<const LockedNode>(input))
            res.emplace(inputPath, lockedInput);
        flattenLockFile(input, inputPath, res);
    }
}

std::string diffLockFiles(const LockFile & oldLocks, const LockFile & newLocks)
{
    std::map<InputPath, std::shared_ptr<const LockedNode>> oldFlat, newFlat;
    flattenLockFile(oldLocks.root, {}, oldFlat);
    flattenLockFile(newLocks.root, {}, newFlat);

    auto i = oldFlat.begin();
    auto j = newFlat.begin();
    std::string res;

    while (i != oldFlat.end() || j != newFlat.end()) {
        if (j != newFlat.end() && (i == oldFlat.end() || i->first > j->first)) {
            res += fmt("* Added '%s': '%s'\n", concatStringsSep("/", j->first), j->second->lockedRef);
            ++j;
        } else if (i != oldFlat.end() && (j == newFlat.end() || i->first < j->first)) {
            res += fmt("* Removed '%s'\n", concatStringsSep("/", i->first));
            ++i;
        } else {
            if (!(i->second->lockedRef == j->second->lockedRef)) {
                assert(i->second->lockedRef.to_string() != j->second->lockedRef.to_string());
                res += fmt("* Updated '%s': '%s' -> '%s'\n",
                    concatStringsSep("/", i->first),
                    i->second->lockedRef,
                    j->second->lockedRef);
            }
            ++i;
            ++j;
        }
    }

    return res;
}

}
