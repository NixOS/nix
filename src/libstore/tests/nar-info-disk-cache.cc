#include "nar-info-disk-cache.hh"

#include <gtest/gtest.h>
#include "sqlite.hh"
#include <sqlite3.h>


namespace nix {

RC_GTEST_PROP(
    NarInfoDiskCacheImpl,
    create_and_read,
    (int prio, bool wantMassQuery)
    )
{
    Path tmpDir = createTempDir();
    AutoDelete delTmpDir(tmpDir);
    Path dbPath(tmpDir + "/test-narinfo-disk-cache.sqlite");
    auto cache = getTestNarInfoDiskCache(dbPath);

    cache->createCache("other://uri", "/nix/storedir", wantMassQuery, prio);
    cache->createCache("other://uri-2", "/nix/storedir", wantMassQuery, prio);

    cache->createCache("the://uri", "/nix/storedir", wantMassQuery, prio);

    {
        auto r = cache->upToDateCacheExists("the://uri");
        ASSERT_TRUE(r);
        ASSERT_EQ(r->priority, prio);
        ASSERT_EQ(r->wantMassQuery, wantMassQuery);
    }

    SQLite db(dbPath);
    SQLiteStmt getIds;
    getIds.create(db, "SELECT id FROM BinaryCaches WHERE url = 'the://uri'");

    int savedId;
    {
        auto q(getIds.use());
        ASSERT_TRUE(q.next());
        savedId = q.getInt(0);
        ASSERT_FALSE(q.next());
    }


    db.exec("UPDATE BinaryCaches SET timestamp = timestamp - 1 - 7 * 24 * 3600;");

    // Relies on memory cache
    {
        auto r = cache->upToDateCacheExists("the://uri");
        ASSERT_TRUE(r);
        ASSERT_EQ(r->priority, prio);
        ASSERT_EQ(r->wantMassQuery, wantMassQuery);
    }

    auto cache2 = getTestNarInfoDiskCache(dbPath);

    {
        auto r = cache2->upToDateCacheExists("the://uri");
        ASSERT_FALSE(r);
    }

    cache2->createCache("the://uri", "/nix/storedir", wantMassQuery, prio);

    {
        auto r = cache->upToDateCacheExists("the://uri");
        ASSERT_TRUE(r);
        ASSERT_EQ(r->priority, prio);
        ASSERT_EQ(r->wantMassQuery, wantMassQuery);
        // FIXME, reproduces #3898
        // ASSERT_EQ(r->id, savedId);
        (void) savedId;
    }

    {
        auto q(getIds.use());
        ASSERT_TRUE(q.next());
        auto currentId = q.getInt(0);
        ASSERT_FALSE(q.next());
        // FIXME, reproduces #3898
        // ASSERT_EQ(currentId, savedId);
        (void) currentId;
    }
}

}
