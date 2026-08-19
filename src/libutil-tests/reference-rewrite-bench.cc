#include "nix/util/hash.hh"
#include "nix/util/types.hh"
#include "nix/util/util.hh"

#include <benchmark/benchmark.h>

#include <format>
#include <numeric>
#include <random>
#include <ranges>
#include <vector>

namespace nix {

/** Roughly 1 placeholder per 5k characters in practice on the store path shape. */
constexpr double defaultCharWeight = 100.0;

/** Deterministic seed for the generators. */
constexpr std::size_t urngSeed = 0;

/** A variant of the placeholder rewrite shape, there are two that interest us, with one exposing a quadratic behaviour.
 */
enum struct RewriteShape {
    /** Placeholder -> store path, as in CA or dynamic derivations, paths are longer and cause the rewrite subject to
       shift indices. */
    PlaceholderToStorePath,
    /** Hash -> hash, as in @ref nix::RewritingSink, causes no shifting. */
    EqualLengthHashes,
};

/** A simple container to keep rewrites and their shape together. */
struct Rewrites
{
    StringMap map;
    RewriteShape shape;
};

/**
 *  Generate a single rewrite pair of the given index and shape.
 *
 *  @param i     the rewrite index in map, so that the rewrites remain subsets of themselves deterministically.
 *  @param shape the shape of the rewrite to generate.
 */
static std::pair<std::string, std::string> makeRewrite(std::size_t i, RewriteShape shape)
{
    auto name = std::format("some-file-{}", i);
    auto hash = hashString(HashAlgorithm::SHA256, std::format("nix-output:{}", name));

    switch (shape) {
    case RewriteShape::PlaceholderToStorePath:
        return {
            std::format("/{}", hash.to_string(HashFormat::Nix32, false)),
            std::format("/nix/store/{}-{}", compressHash(hash, 20).to_string(HashFormat::Nix32, false), name),
        };
    case RewriteShape::EqualLengthHashes: {
        auto other = hashString(HashAlgorithm::SHA256, "nix-scratch:" + name);

        return {
            compressHash(hash, 20).to_string(HashFormat::Nix32, false),
            compressHash(other, 20).to_string(HashFormat::Nix32, false),
        };
    }
    }

    unreachable();
}

/**
 *  Generate a map of rewrites.
 *
 *  @param count the number of rewrites to generate.
 *  @param shape the shape of the rewrites to generate.
 */
static Rewrites makeRewrites(std::size_t count, RewriteShape shape)
{
    Rewrites rewrites;

    rewrites.shape = shape;

    for (std::size_t i = 0; i < count; ++i) {
        auto [key, value] = makeRewrite(i, shape);

        rewrites.map.insert_or_assign(std::move(key), std::move(value));
    }

    return rewrites;
}

/**
 * Generate a random byte sequence with placeholders taken from a map of rewrites.
 *
 * @param urng                 the random generator to use.
 * @param length               the byte length of the resulting string.
 * @param charWeight           the relative frequency of a non-reference byte in the sequence.
 * @param rewrites             the collection from which to sample rewriteable placeholders.
 * @param realPlaceholderRatio the ratio of placeholders that will have actual replacements in `rewrites`.
 */
static std::string randomBytesWithReferences(
    std::mt19937 & urng, std::size_t length, double charWeight, Rewrites & rewrites, double realPlaceholderRatio = 1.0)
{
    std::string result;

    result.reserve(length);

    // NOTE: std::uniform_int_distribution isn't guaranteed to be implemented for char.
    auto contentDistribution = std::uniform_int_distribution<int>{
        std::numeric_limits<char>::min(),
        std::numeric_limits<char>::max(),
    };

    std::vector<std::string> placeholders;
    std::size_t rewriteCount = rewrites.map.size();

    placeholders.reserve(rewriteCount);

    // Optional partial replacement with decoys
    std::size_t trueReplacements = rewriteCount * realPlaceholderRatio;
    std::size_t decoyReplacements = rewriteCount - trueReplacements;
    std::ranges::sample(std::views::keys(rewrites.map), std::back_inserter(placeholders), trueReplacements, urng);

    for (std::size_t i = 0; i < decoyReplacements; i++) {
        auto [placeholder, _] = makeRewrite(rewriteCount + i, rewrites.shape);

        placeholders.push_back(placeholder);
    }

    placeholders.shrink_to_fit();

    if (!placeholders.empty()) {
        auto placeholderDistribution = std::uniform_int_distribution<std::size_t>(0, placeholders.size() - 1);
        auto firstPlaceholder = *placeholders.begin();
        std::discrete_distribution<std::size_t> branchDist{1.0, firstPlaceholder.size() * charWeight};

        while (result.size() < length) {
            if (branchDist(urng) == 0)
                result += placeholders[placeholderDistribution(urng)];
            else
                result.push_back(static_cast<char>(contentDistribution(urng)));
        }
    } else {
        for (std::size_t i = 0; i < length; i++) {
            result.push_back(static_cast<char>(contentDistribution(urng)));
        }
    }

    result.resize(length);

    return result;
}

// Benchmarks

/**
 * This one shows O(n²) behaviour from string replacement having to copy the remainder of the subject each time the
 * replacement is longer than the placeholder.
 */
static void BM_RewriteStrings_ToStorePath_VarySubjectLength(benchmark::State & state)
{
    std::mt19937 urng(urngSeed);

    auto subjectLength = state.range();
    auto rewriteCount = 2 << 6;

    Rewrites rewrites = makeRewrites(rewriteCount, RewriteShape::PlaceholderToStorePath);
    std::string contents = randomBytesWithReferences(urng, subjectLength, defaultCharWeight, rewrites);

    // Ensure we actually rewrite something
    assert(nix::rewriteStrings(contents, rewrites.map) != contents);

    state.SetComplexityN(subjectLength);

    std::size_t processed = 0;

    for (auto _ : state) {
        auto result = nix::rewriteStrings(contents, rewrites.map);

        benchmark::DoNotOptimize(result);

        processed += contents.size();
    }

    state.SetBytesProcessed(processed);
}

BENCHMARK(BM_RewriteStrings_ToStorePath_VarySubjectLength)
    ->RangeMultiplier(2)
    ->Range(2 << 14, 2 << 23)
    ->Complexity(benchmark::oNSquared)
    ->Unit(benchmark::kMillisecond);

/**
 * This one shows O(n²) behaviour in typical real-world granular dynamic derivation builds, where both the number of
 * paths to rewrite and artefact size end up growing. Notice how it still occurs even if we don't need to shift the
 * string.
 */
static void BM_RewriteStrings_ToHash_VaryBothSubjectLengthAndRewriteCount(benchmark::State & state)
{
    std::mt19937 urng(urngSeed);

    auto subjectLength = state.range();
    auto rewriteCount = subjectLength / (2 << 12);

    Rewrites rewrites = makeRewrites(rewriteCount, RewriteShape::EqualLengthHashes);
    std::string contents = randomBytesWithReferences(urng, subjectLength, defaultCharWeight, rewrites);

    // Ensure we actually rewrite something
    assert(nix::rewriteStrings(contents, rewrites.map) != contents);

    state.SetComplexityN(subjectLength);

    std::size_t processed = 0;

    for (auto _ : state) {
        auto result = nix::rewriteStrings(contents, rewrites.map);

        benchmark::DoNotOptimize(result);

        processed += contents.size();
    }

    state.SetBytesProcessed(processed);
}

BENCHMARK(BM_RewriteStrings_ToHash_VaryBothSubjectLengthAndRewriteCount)
    ->RangeMultiplier(2)
    ->Range(2 << 20, 2 << 23)
    ->Complexity(benchmark::oNSquared)
    ->Unit(benchmark::kMillisecond);

/**
 * One component of the quadratic behaviour – runtime is O(n) in rewrite count.
 */
static void BM_RewriteStrings_ToHash_VaryRewriteCount(benchmark::State & state)
{
    std::mt19937 urng(urngSeed);

    auto subjectLength = 2 << 20;
    auto rewriteCount = state.range();

    Rewrites rewrites = makeRewrites(rewriteCount, RewriteShape::EqualLengthHashes);
    std::string contents = randomBytesWithReferences(urng, subjectLength, defaultCharWeight, rewrites);

    // Ensure we actually rewrite something
    assert(nix::rewriteStrings(contents, rewrites.map) != contents);

    state.SetComplexityN(rewriteCount);

    std::size_t processed = 0;

    for (auto _ : state) {
        auto result = nix::rewriteStrings(contents, rewrites.map);

        benchmark::DoNotOptimize(result);

        processed += contents.size();
    }

    state.SetBytesProcessed(processed);
}

BENCHMARK(BM_RewriteStrings_ToHash_VaryRewriteCount)
    ->RangeMultiplier(2)
    ->Range(8, 8'192)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kMillisecond);

/**
 * The other component of the quadratic behaviour – runtime is O(n) in subject length.
 */
static void BM_RewriteStrings_ToHash_VarySubjectLength(benchmark::State & state)
{
    std::mt19937 urng(urngSeed);

    auto subjectLength = state.range();
    auto rewriteCount = 2 << 6;

    Rewrites rewrites = makeRewrites(rewriteCount, RewriteShape::EqualLengthHashes);
    std::string contents = randomBytesWithReferences(urng, subjectLength, defaultCharWeight, rewrites);

    // Ensure we actually rewrite something
    assert(nix::rewriteStrings(contents, rewrites.map) != contents);

    state.SetComplexityN(subjectLength);

    std::size_t processed = 0;

    for (auto _ : state) {
        auto result = nix::rewriteStrings(contents, rewrites.map);

        benchmark::DoNotOptimize(result);

        processed += contents.size();
    }

    state.SetBytesProcessed(processed);
}

BENCHMARK(BM_RewriteStrings_ToHash_VarySubjectLength)
    ->RangeMultiplier(2)
    ->Range(2 << 14, 2 << 23)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kMillisecond);

/**
 * Runtime is not affected by how many paths are actually present in the subject.
 */
static void BM_RewriteStrings_ToHash_VaryRewriteRatio(benchmark::State & state)
{
    std::mt19937 urng(urngSeed);

    auto subjectLength = 2 << 20;
    auto rewriteCount = 2 << 10;
    auto rewriteRatio = state.range() / 100.0;

    Rewrites rewrites = makeRewrites(rewriteCount, RewriteShape::EqualLengthHashes);
    std::string contents = randomBytesWithReferences(urng, subjectLength, defaultCharWeight, rewrites, rewriteRatio);

    // Ensure we actually rewrite something
    if (state.range() > 0) {
        assert(nix::rewriteStrings(contents, rewrites.map) != contents);
    }

    state.SetComplexityN(state.range());

    std::size_t processed = 0;

    for (auto _ : state) {
        auto result = nix::rewriteStrings(contents, rewrites.map);

        benchmark::DoNotOptimize(result);

        processed += contents.size();
    }

    state.SetBytesProcessed(processed);
}

BENCHMARK(BM_RewriteStrings_ToHash_VaryRewriteRatio)
    ->DenseRange(0, 100, 10)
    ->Complexity(benchmark::o1)
    ->Unit(benchmark::kMillisecond);

/**
 * Runtime is still O(n) in subject length even if there are no paths to rewrite. The culprit of this linearity is
 * copying the subject into the function.
 */
static void BM_RewriteStrings_EmptyRewrites_VarySubjectLength(benchmark::State & state)
{
    std::mt19937 urng(urngSeed);

    auto subjectLength = state.range();

    Rewrites rewrites;
    std::string contents = randomBytesWithReferences(urng, subjectLength, defaultCharWeight, rewrites);

    // Ensure nothing had been rewritten
    assert(nix::rewriteStrings(contents, rewrites.map) == contents);

    state.SetComplexityN(subjectLength);

    std::size_t processed = 0;

    for (auto _ : state) {
        auto result = nix::rewriteStrings(contents, rewrites.map);

        benchmark::DoNotOptimize(result);

        processed += contents.size();
    }

    state.SetBytesProcessed(processed);
}

BENCHMARK(BM_RewriteStrings_EmptyRewrites_VarySubjectLength)
    ->RangeMultiplier(2)
    ->Range(2 << 14, 2 << 22)
    ->Complexity(benchmark::oN)
    ->Unit(benchmark::kMillisecond);

} // namespace nix
