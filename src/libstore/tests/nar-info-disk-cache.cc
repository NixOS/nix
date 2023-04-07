#include "nar-info-disk-cache.hh"

#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>
#include "sqlite.hh"
#include <sqlite3.h>


namespace nix {

TEST(NarInfoDiskCacheImpl, create_and_read) {
    // This is a large single test to avoid some setup overhead.

    int prio = 12345;
    bool wantMassQuery = true;

    Path tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir);
    Path dbPath(tmpDir + "/test-narinfo-disk-cache.sqlite");

    int savedId;
    int barId;
    SQLite db;
    SQLiteStmt getIds;

    {
        auto cache = getTestNarInfoDiskCache(dbPath);

        // Set up "background noise" and check that different caches receive different ids
        {
            auto bc1 = cache->createCache("https://bar", "/nix/storedir", wantMassQuery, prio);
            auto bc2 = cache->createCache("https://xyz", "/nix/storedir", false, 12);
            ASSERT_NE(bc1, bc2);
            barId = bc1;
        }

        // Check that the fields are saved and returned correctly. This does not test
        // the select statement yet, because of in-memory caching.
        savedId = cache->createCache("http://foo", "/nix/storedir", wantMassQuery, prio);;
        {
            auto r = cache->upToDateCacheExists("http://foo");
            ASSERT_TRUE(r);
            ASSERT_EQ(r->priority, prio);
            ASSERT_EQ(r->wantMassQuery, wantMassQuery);
            ASSERT_EQ(savedId, r->id);
        }

        // We're going to pay special attention to the id field because we had a bug
        // that changed it.
        db = SQLite(dbPath);
        getIds.create(db, "select id from BinaryCaches where url = 'http://foo'");

        {
            auto q(getIds.use());
            ASSERT_TRUE(q.next());
            ASSERT_EQ(savedId, q.getInt(0));
            ASSERT_FALSE(q.next());
        }

        // Pretend that the caches are older, but keep one up to date, as "background noise"
        db.exec("update BinaryCaches set timestamp = timestamp - 1 - 7 * 24 * 3600 where url <> 'https://xyz';");

        // This shows that the in-memory cache works
        {
            auto r = cache->upToDateCacheExists("http://foo");
            ASSERT_TRUE(r);
            ASSERT_EQ(r->priority, prio);
            ASSERT_EQ(r->wantMassQuery, wantMassQuery);
        }
    }

    {
        // We can't clear the in-memory cache, so we use a new cache object. This is
        // more realistic anyway.
        auto cache2 = getTestNarInfoDiskCache(dbPath);

        {
            auto r = cache2->upToDateCacheExists("http://foo");
            ASSERT_FALSE(r);
        }

        // "Update", same data, check that the id number is reused
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

        // Check that the fields can be modified, and the id remains the same
        {
            auto r0 = cache2->upToDateCacheExists("https://bar");
            ASSERT_FALSE(r0);

            cache2->createCache("https://bar", "/nix/storedir", !wantMassQuery, prio + 10);
            auto r = cache2->upToDateCacheExists("https://bar");
            ASSERT_EQ(r->wantMassQuery, !wantMassQuery);
            ASSERT_EQ(r->priority, prio + 10);
            ASSERT_EQ(r->id, barId);
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

}
