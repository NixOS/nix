#include <gtest/gtest.h>
#include <rapidcheck/gtest.h>
#include "nix/store/sqlite.hh"
#include "nix/util/util.hh"
#include "nix/util/finally.hh"
#include <nlohmann/json.hpp>
#include <fstream>
#include <thread>
#include <chrono>

namespace nix {

class SQLiteProfilingTest : public ::testing::Test
{
protected:
    Path tmpDir;
    AutoDelete delTmpDir;

    void SetUp() override
    {
        tmpDir = createTempDir();
        delTmpDir = AutoDelete(tmpDir);
        // Ensure clean state
        unsetenv("NIX_SQLITE_PROFILE");
    }

    void TearDown() override
    {
        // Clean up profiling state
        unsetenv("NIX_SQLITE_PROFILE");
    }

    bool validateJsonLines(const Path & path)
    {
        std::ifstream file(path);
        if (!file.is_open())
            return false;

        std::string line;
        int lineCount = 0;
        bool hasStart = false;
        bool hasSummary = false;

        while (std::getline(file, line)) {
            lineCount++;
            try {
                auto json = nlohmann::json::parse(line);

                // Check for required event types
                if (json.contains("type")) {
                    std::string type = json["type"];
                    if (type == "start")
                        hasStart = true;
                    else if (type == "summary")
                        hasSummary = true;
                } else {
                    // Query event - check required fields
                    EXPECT_TRUE(json.contains("timestamp_ms"));
                    EXPECT_TRUE(json.contains("database"));
                    EXPECT_TRUE(json.contains("execution_time_ms"));
                    EXPECT_TRUE(json.contains("query"));
                }
            } catch (const std::exception & e) {
                ADD_FAILURE() << "JSON parse error at line " << lineCount << ": " << e.what();
                return false;
            }
        }

        return lineCount > 0 && hasStart;
    }
};

TEST_F(SQLiteProfilingTest, disabled_by_default)
{
    // Profiling should be disabled when env var is not set
    SQLite db(":memory:");
    db.exec("CREATE TABLE test (id INTEGER)");

    // No profile file should be created
    Path defaultProfile = "nix-sqlite-profile.jsonl";
    ASSERT_FALSE(pathExists(defaultProfile));
}

TEST_F(SQLiteProfilingTest, basic_profiling)
{
    Path profilePath = tmpDir + "/profile.jsonl";
    setenv("NIX_SQLITE_PROFILE", profilePath.c_str(), 1);
    Finally resetEnv([]() { unsetenv("NIX_SQLITE_PROFILE"); });

    {
        SQLite db(":memory:");
        db.exec("CREATE TABLE test (id INTEGER PRIMARY KEY, data TEXT)");
        db.exec("INSERT INTO test (data) VALUES ('hello')");

        auto stmt = SQLiteStmt(db, "SELECT * FROM test WHERE id = ?");
        stmt.use()(1).exec();
    }

    // Force cleanup to get summary
    // In real code, this happens on program exit

    // Verify profile was created
    ASSERT_TRUE(pathExists(profilePath));

    // Validate JSON output
    ASSERT_TRUE(validateJsonLines(profilePath));
}

TEST_F(SQLiteProfilingTest, file_database_profiling)
{
    Path profilePath = tmpDir + "/profile.jsonl";
    Path dbPath = tmpDir + "/test.db";
    setenv("NIX_SQLITE_PROFILE", profilePath.c_str(), 1);
    Finally resetEnv([]() { unsetenv("NIX_SQLITE_PROFILE"); });

    {
        SQLite db(dbPath);
        db.exec("CREATE TABLE cache (key TEXT PRIMARY KEY, value TEXT)");

        // Use prepared statement
        auto stmt = SQLiteStmt(db, "INSERT INTO cache (key, value) VALUES (?, ?)");
        stmt.use()("key1")("value1").exec();
    }

    // Check that database path is recorded
    std::ifstream file(profilePath);
    std::string line;
    bool foundDbPath = false;

    while (std::getline(file, line)) {
        if (line.find(dbPath) != std::string::npos) {
            foundDbPath = true;
            break;
        }
    }

    ASSERT_TRUE(foundDbPath) << "Database path not found in profile";
}

TEST_F(SQLiteProfilingTest, concurrent_profiling)
{
    Path profilePath = tmpDir + "/profile.jsonl";
    setenv("NIX_SQLITE_PROFILE", profilePath.c_str(), 1);
    Finally resetEnv([]() { unsetenv("NIX_SQLITE_PROFILE"); });

    const int numThreads = 4;
    const int queriesPerThread = 10;

    auto threadFunc = [&](int threadId) {
        SQLite db(":memory:");
        db.exec("CREATE TABLE test (id INTEGER, thread_id INTEGER)");

        for (int i = 0; i < queriesPerThread; i++) {
            auto stmt = SQLiteStmt(db, "INSERT INTO test (id, thread_id) VALUES (?, ?)");
            stmt.use()(i)(threadId).exec();
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < numThreads; i++) {
        threads.emplace_back(threadFunc, i);
    }

    for (auto & t : threads) {
        t.join();
    }

    // Verify all queries were logged
    ASSERT_TRUE(pathExists(profilePath));
    ASSERT_TRUE(validateJsonLines(profilePath));

    // Count query events
    std::ifstream file(profilePath);
    std::string line;
    int queryCount = 0;

    while (std::getline(file, line)) {
        auto json = nlohmann::json::parse(line);
        if (json.contains("query")) {
            queryCount++;
        }
    }

    // Should have at least the expected number of queries
    // (CREATE TABLE + INSERTs) * threads
    int expectedMin = (1 + queriesPerThread) * numThreads;
    EXPECT_GE(queryCount, expectedMin);
}

TEST_F(SQLiteProfilingTest, profile_with_special_path)
{
    // Test with "1" which should use default filename
    setenv("NIX_SQLITE_PROFILE", "1", 1);
    Finally resetEnv([]() { unsetenv("NIX_SQLITE_PROFILE"); });

    {
        SQLite db(":memory:");
        db.exec("SELECT 1");
    }

    // Should create default file
    Path defaultProfile = "nix-sqlite-profile.jsonl";
    ASSERT_TRUE(pathExists(defaultProfile));

    // Clean up
    unlink(defaultProfile.c_str());
}

TEST_F(SQLiteProfilingTest, invalid_profile_path)
{
    // Test with non-existent directory
    Path invalidPath = "/non/existent/directory/profile.jsonl";
    setenv("NIX_SQLITE_PROFILE", invalidPath.c_str(), 1);
    Finally resetEnv([]() { unsetenv("NIX_SQLITE_PROFILE"); });

    // Should not crash, just disable profiling
    EXPECT_NO_THROW({
        SQLite db(":memory:");
        db.exec("SELECT 1");
    });
}

TEST_F(SQLiteProfilingTest, query_with_parameters)
{
    Path profilePath = tmpDir + "/profile.jsonl";
    setenv("NIX_SQLITE_PROFILE", profilePath.c_str(), 1);
    Finally resetEnv([]() { unsetenv("NIX_SQLITE_PROFILE"); });

    {
        SQLite db(":memory:");
        db.exec("CREATE TABLE users (id INTEGER, name TEXT)");

        auto stmt = SQLiteStmt(db, "INSERT INTO users (id, name) VALUES (?, ?)");
        stmt.use()(42)("John Doe").exec();

        auto selectStmt = SQLiteStmt(db, "SELECT name FROM users WHERE id = ?");
        selectStmt.use()(42).exec();
    }

    // Check that expanded SQL is captured
    std::ifstream file(profilePath);
    std::string line;
    bool foundExpandedInsert = false;
    bool foundExpandedSelect = false;

    while (std::getline(file, line)) {
        auto json = nlohmann::json::parse(line);
        if (json.contains("query")) {
            std::string query = json["query"];
            if (query.find("INSERT INTO users (id, name) VALUES (42, 'John Doe')") != std::string::npos) {
                foundExpandedInsert = true;
            }
            if (query.find("SELECT name FROM users WHERE id = 42") != std::string::npos) {
                foundExpandedSelect = true;
            }
        }
    }

    EXPECT_TRUE(foundExpandedInsert) << "Expanded INSERT not found";
    EXPECT_TRUE(foundExpandedSelect) << "Expanded SELECT not found";
}

// Property-based test
RC_GTEST_PROP(SQLiteProfilingTest, random_operations, ())
{
    Path profilePath = tmpDir + "/profile-prop.jsonl";
    setenv("NIX_SQLITE_PROFILE", profilePath.c_str(), 1);
    Finally resetEnv([]() { unsetenv("NIX_SQLITE_PROFILE"); });

    auto numOps = *rc::gen::inRange(1, 20);

    {
        SQLite db(":memory:");
        db.exec("CREATE TABLE prop_test (id INTEGER, value INTEGER)");

        for (int i = 0; i < numOps; i++) {
            auto stmt = SQLiteStmt(db, "INSERT INTO prop_test (id, value) VALUES (?, ?)");
            stmt.use()(i)(*rc::gen::arbitrary<int>()).exec();
        }
    }

    RC_ASSERT(pathExists(profilePath));
    RC_ASSERT(validateJsonLines(profilePath));
}

}