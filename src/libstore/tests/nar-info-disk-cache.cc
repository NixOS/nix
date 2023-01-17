#include "nar-info-disk-cache.hh"

#include <gtest/gtest.h>
#include "sqlite.hh"
#include <sqlite3.h>


namespace nix {

TEST(NarInfoDiskCacheImpl, create_and_read) {
    int prio = 12345;
    bool wantMassQuery = true;

    Path tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir);
    Path dbPath(tmpDir + "/test-narinfo-disk-cache.sqlite");
    auto cache = getTestNarInfoDiskCache(dbPath);

    {
        auto bc1 = cache->createCache("https://bar", "/nix/storedir", wantMassQuery, prio);
        auto bc2 = cache->createCache("https://xyz", "/nix/storedir", false, 12);
        ASSERT_NE(bc1, bc2);
    }

    int savedId = cache->createCache("http://foo", "/nix/storedir", wantMassQuery, prio);;
    {
        auto r = cache->upToDateCacheExists("http://foo");
        ASSERT_TRUE(r);
        ASSERT_EQ(r->priority, prio);
        ASSERT_EQ(r->wantMassQuery, wantMassQuery);
        ASSERT_EQ(savedId, r->id);
    }

    SQLite db(dbPath);
    SQLiteStmt getIds;
    getIds.create(db, "SELECT id FROM BinaryCaches WHERE url = 'http://foo'");

    {
        auto q(getIds.use());
        ASSERT_TRUE(q.next());
        ASSERT_EQ(savedId, q.getInt(0));
        ASSERT_FALSE(q.next());
    }

    db.exec("UPDATE BinaryCaches SET timestamp = timestamp - 1 - 7 * 24 * 3600;");

    // Relies on memory cache
    {
        auto r = cache->upToDateCacheExists("http://foo");
        ASSERT_TRUE(r);
        ASSERT_EQ(r->priority, prio);
        ASSERT_EQ(r->wantMassQuery, wantMassQuery);
    }

    // We can't clear the in-memory cache, so we use a new cache object.
    auto cache2 = getTestNarInfoDiskCache(dbPath);

    {
        auto r = cache2->upToDateCacheExists("http://foo");
        ASSERT_FALSE(r);
    }

    // Update, same data, check that the id number is reused
    cache2->createCache("http://foo", "/nix/storedir", wantMassQuery, prio);

    {
        auto r = cache2->upToDateCacheExists("http://foo");
        ASSERT_TRUE(r);
        ASSERT_EQ(r->priority, prio);
        ASSERT_EQ(r->wantMassQuery, wantMassQuery);
        ASSERT_EQ(r->id, savedId);
    }

    {
        auto q(getIds.use());
        ASSERT_TRUE(q.next());
        auto currentId = q.getInt(0);
        ASSERT_FALSE(q.next());
        ASSERT_EQ(currentId, savedId);
    }

    // Check that the fields can be modified
    {
        auto r0 = cache2->upToDateCacheExists("https://bar");
        ASSERT_FALSE(r0);

        cache2->createCache("https://bar", "/nix/storedir", !wantMassQuery, prio + 10);
        auto r = cache2->upToDateCacheExists("https://bar");
        ASSERT_EQ(r->wantMassQuery, !wantMassQuery);
        ASSERT_EQ(r->priority, prio + 10);
    }

    // // Force update (no use case yet; we only retrieve cache metadata when stale based on timestamp)
    // {
    //     cache2->createCache("https://bar", "/nix/storedir", wantMassQuery, prio + 20);
    //     auto r = cache2->upToDateCacheExists("https://bar");
    //     ASSERT_EQ(r->wantMassQuery, wantMassQuery);
    //     ASSERT_EQ(r->priority, prio + 20);
    // }

}

}
