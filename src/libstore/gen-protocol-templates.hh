/* Shared code between worker and serv protocols, injected into proper header.
 */

template<typename T>
std::vector<T> read(const Store & store, Source & from, Phantom<std::vector<T>> _)
{
    std::vector<T> resSet;
    auto size = readNum<size_t>(from);
    while (size--) {
        resSet.push_back(read(store, from, Phantom<T> {}));
    }
    return resSet;
}

template<typename T>
void write(const Store & store, Sink & out, const std::vector<T> & resSet)
{
    out << resSet.size();
    for (auto & key : resSet) {
        write(store, out, key);
    }
}

template<typename T>
std::set<T> read(const Store & store, Source & from, Phantom<std::set<T>> _)
{
    std::set<T> resSet;
    auto size = readNum<size_t>(from);
    while (size--) {
        resSet.insert(read(store, from, Phantom<T> {}));
    }
    return resSet;
}

template<typename T>
void write(const Store & store, Sink & out, const std::set<T> & resSet)
{
    out << resSet.size();
    for (auto & key : resSet) {
        write(store, out, key);
    }
}

template<typename K, typename V>
std::map<K, V> read(const Store & store, Source & from, Phantom<std::map<K, V>> _)
{
    std::map<K, V> resMap;
    auto size = readNum<size_t>(from);
    while (size--) {
        auto k = read(store, from, Phantom<K> {});
        auto v = read(store, from, Phantom<V> {});
        resMap.insert_or_assign(std::move(k), std::move(v));
    }
    return resMap;
}

template<typename K, typename V>
void write(const Store & store, Sink & out, const std::map<K, V> & resMap)
{
    out << resMap.size();
    for (auto & i : resMap) {
        write(store, out, i.first);
        write(store, out, i.second);
    }
}
